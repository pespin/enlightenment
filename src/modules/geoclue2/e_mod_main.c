#include "e.h"

#include "gen/eldbus_geo_clue2_manager.h"
#include "gen/eldbus_geo_clue2_client.h"
#include "gen/eldbus_geo_clue2_location.h"

/* gadcon requirements */
static E_Gadcon_Client *_gc_init(E_Gadcon *gc, const char *name, const char *id, const char *style);
static void _gc_shutdown(E_Gadcon_Client *gcc);
static void _gc_orient(E_Gadcon_Client *gcc, E_Gadcon_Orient orient);
static const char *_gc_label(const E_Gadcon_Client_Class *client_class);
static Evas_Object *_gc_icon(const E_Gadcon_Client_Class *client_class, Evas *evas);
static const char *_gc_id_new(const E_Gadcon_Client_Class *client_class);

/* and actually define the gadcon class that this module provides (just 1) */
static const E_Gadcon_Client_Class _gadcon_class =
{
   GADCON_CLIENT_CLASS_VERSION,
     "geolocation",
     {
        _gc_init, _gc_shutdown, _gc_orient, _gc_label, _gc_icon, _gc_id_new, NULL, NULL
     },
   E_GADCON_CLIENT_STYLE_PLAIN
};

/* actual module specifics */
typedef struct _Instance Instance;

struct _Instance
{
   E_Gadcon_Client *gcc;
   Evas_Object     *o_geoclue2;
   Eina_Bool       in_use;
   Eldbus_Connection *conn;
   Eldbus_Service_Interface *iface;
   Eldbus_Proxy *manager;
   Eldbus_Proxy *client;
   Eldbus_Proxy *location;
   double latitude;
   double longitude;
   double accuracy;
   double altitude;
   const char *description;
};

static Eina_List *geoclue2_instances = NULL;
static E_Module *geoclue2_module = NULL;

void
cb_client_start(Eldbus_Proxy *proxy EINA_UNUSED, void *data, Eldbus_Pending *pending EINA_UNUSED,
		Eldbus_Error_Info *error EINA_UNUSED)
{
   Instance *inst = data;

   DBG("Client proxy start callback received");

   edje_object_signal_emit(inst->o_geoclue2, "e,state,location_on", "e");
}

void
cb_client_stop(Eldbus_Proxy *proxy EINA_UNUSED, void *data, Eldbus_Pending *pending EINA_UNUSED,
		Eldbus_Error_Info *error EINA_UNUSED)
{
   Instance *inst = data;

   DBG("Client proxy stop callback received");

   edje_object_signal_emit(inst->o_geoclue2, "e,state,location_off", "e");
}

static void
_geoclue2_cb_mouse_down(void *data, Evas *evas __UNUSED__, Evas_Object *obj __UNUSED__, void *event)
{
   Instance *inst = data;
   Evas_Event_Mouse_Down *ev = event;

   if (ev->button == 1)
     {
        DBG("**** DEBUG **** Left mouse button clicked on icon");
        geo_clue2_client_start_call(inst->client, cb_client_start, inst);
     }
   if (ev->button == 3)
     {
        E_Zone *zone;
        E_Menu *m;
        E_Menu_Item *mi;
        int x, y;

        DBG("**** DEBUG **** Right mouse button clicked on icon");
        geo_clue2_client_stop_call(inst->client, cb_client_stop, inst);

        zone = e_util_zone_current_get(e_manager_current_get());

        m = e_menu_new();

        mi = e_menu_item_new(m);
        e_menu_item_label_set(mi, _("Settings"));
        e_util_menu_item_theme_icon_set(mi, "configure");

        m = e_gadcon_client_util_menu_items_append(inst->gcc, m, 0);

        e_gadcon_canvas_zone_geometry_get(inst->gcc->gadcon, &x, &y, NULL, NULL);
        e_menu_activate_mouse(m, zone, x + ev->output.x, y + ev->output.y,
                              1, 1, E_MENU_POP_DIRECTION_AUTO, ev->timestamp);
        evas_event_feed_mouse_up(inst->gcc->gadcon->evas, ev->button,
                                 EVAS_BUTTON_NONE, ev->timestamp, NULL);
     }
}

void
cb_location_prop_latitude_get(void *data EINA_UNUSED, Eldbus_Pending *p EINA_UNUSED,
		              const char *propname EINA_UNUSED, Eldbus_Proxy *proxy EINA_UNUSED,
		              Eldbus_Error_Info *error_info EINA_UNUSED, double value)
{
   Instance *inst = data;
   inst->latitude = value;

   DBG("Location property Latitude: %f", value);
}

void
cb_location_prop_longitude_get(void *data EINA_UNUSED, Eldbus_Pending *p EINA_UNUSED,
		               const char *propname EINA_UNUSED, Eldbus_Proxy *proxy EINA_UNUSED,
		               Eldbus_Error_Info *error_info EINA_UNUSED, double value)
{
   Instance *inst = data;
   inst->longitude = value;

   DBG("Location property Longitude: %f", value);
}

void
cb_location_prop_accuracy_get(void *data EINA_UNUSED, Eldbus_Pending *p EINA_UNUSED,
		              const char *propname EINA_UNUSED, Eldbus_Proxy *proxy EINA_UNUSED,
		              Eldbus_Error_Info *error_info EINA_UNUSED, double value)
{
   Instance *inst = data;
   inst->accuracy = value;

   DBG("Location property Accuracy: %f", value);
}

void
cb_location_prop_altitude_get(void *data EINA_UNUSED, Eldbus_Pending *p EINA_UNUSED,
		              const char *propname EINA_UNUSED, Eldbus_Proxy *proxy EINA_UNUSED,
		              Eldbus_Error_Info *error_info EINA_UNUSED, double value)
{
   Instance *inst = data;
   inst->altitude = value;

   DBG("Location property Altitude: %f", value);
}

void
cb_location_prop_description_get(void *data EINA_UNUSED, Eldbus_Pending *p EINA_UNUSED,
		                 const char *propname EINA_UNUSED, Eldbus_Proxy *proxy EINA_UNUSED,
		                 Eldbus_Error_Info *error_info EINA_UNUSED, const char *value)
{
   Instance *inst = data;
   inst->description = value;

   DBG("Location property Description: %s", value);
}

void
cb_client_location_updated_signal(void *data, const Eldbus_Message *msg)
{
   const char *new_path, *old_path;
   Instance *inst = data;

   DBG("Client LocationUpdated signal received");

   if (!eldbus_message_arguments_get(msg, "oo", &old_path, &new_path))
     {
        ERR("Error: could not get location update");
        return;
     }
   DBG("Client signal location path old: %s", old_path);
   DBG("Client signal location path new: %s", new_path);

   inst->location = geo_clue2_location_proxy_get(inst->conn, "org.freedesktop.GeoClue2", new_path);
   if (!inst->location)
     {
        ERR("Error: could not connect to GeoClue2 location proxy");
        return;
     }

   geo_clue2_location_latitude_propget(inst->location, cb_location_prop_latitude_get, inst);
   geo_clue2_location_longitude_propget(inst->location, cb_location_prop_longitude_get, inst);
   geo_clue2_location_accuracy_propget(inst->location, cb_location_prop_accuracy_get, inst);
   geo_clue2_location_altitude_propget(inst->location, cb_location_prop_altitude_get, inst);
   geo_clue2_location_description_propget(inst->location, cb_location_prop_description_get, inst);
}

void
cb_client_object_get(Eldbus_Proxy *proxy EINA_UNUSED, void *data, Eldbus_Pending *pending EINA_UNUSED,
		     Eldbus_Error_Info *error EINA_UNUSED, const char *client_path)
{
   Instance *inst = data;

   DBG("Client object path: %s", client_path);
   inst->client = geo_clue2_client_proxy_get(inst->conn, "org.freedesktop.GeoClue2", client_path);
   if (!inst->client)
     {
        ERR("Error: could not connect to GeoClue2 client proxy");
        return;
     }

   eldbus_proxy_signal_handler_add(inst->client, "LocationUpdated", cb_client_location_updated_signal, inst);
}

static E_Gadcon_Client *
_gc_init(E_Gadcon *gc, const char *name, const char *id, const char *style)
{
   Evas_Object *o;
   E_Gadcon_Client *gcc;
   Instance *inst;

   eldbus_init();
   inst = E_NEW(Instance, 1);

   o = edje_object_add(gc->evas);
   e_theme_edje_object_set(o, "base/theme/modules/geolocation",
                           "e/modules/geolocation/main");
   evas_object_show(o);

   gcc = e_gadcon_client_new(gc, name, id, style, o);
   gcc->data = inst;

   inst->gcc = gcc;
   inst->o_geoclue2 = o;

   inst->in_use = EINA_FALSE;
   edje_object_signal_emit(inst->o_geoclue2, "e,state,location_off", "e");

   evas_object_event_callback_add(inst->o_geoclue2,
                                  EVAS_CALLBACK_MOUSE_DOWN,
                                  _geoclue2_cb_mouse_down,
                                  inst);

   geoclue2_instances = eina_list_append(geoclue2_instances, inst);

   inst->conn = eldbus_connection_get(ELDBUS_CONNECTION_TYPE_SYSTEM);
   if (!inst->conn)
     {
        ERR("Error: could not get system bus.");
        return NULL;
     }

   inst->manager = geo_clue2_manager_proxy_get(inst->conn, "org.freedesktop.GeoClue2", "/org/freedesktop/GeoClue2/Manager");
   if (!inst->manager)
     {
        ERR("Error: could not connect to GeoClue2 manager proxy");
        return NULL;
     }

   geo_clue2_manager_get_client_call(inst->manager, cb_client_object_get, inst);

   return gcc;
}

static void
_gc_shutdown(E_Gadcon_Client *gcc)
{
   Instance *inst;

   inst = gcc->data;
   geoclue2_instances = eina_list_remove(geoclue2_instances, inst);
   evas_object_del(inst->o_geoclue2);
   geo_clue2_manager_proxy_unref(inst->location);
   geo_clue2_manager_proxy_unref(inst->client);
   geo_clue2_manager_proxy_unref(inst->manager);
   eldbus_connection_unref(inst->conn);
   free(inst);
   eldbus_shutdown();
}

static void
_gc_orient(E_Gadcon_Client *gcc, E_Gadcon_Orient orient __UNUSED__)
{
   Instance *inst;
   Evas_Coord mw, mh;

   inst = gcc->data;
   mw = 0, mh = 0;
   edje_object_size_min_get(inst->o_geoclue2, &mw, &mh);
   if ((mw < 1) || (mh < 1))
     edje_object_size_min_calc(inst->o_geoclue2, &mw, &mh);
   if (mw < 4) mw = 4;
   if (mh < 4) mh = 4;
   e_gadcon_client_aspect_set(gcc, mw, mh);
   e_gadcon_client_min_size_set(gcc, mw, mh);
}

static const char *
_gc_label(const E_Gadcon_Client_Class *client_class __UNUSED__)
{
   return _("Geolocation");
}

static Evas_Object *
_gc_icon(const E_Gadcon_Client_Class *client_class __UNUSED__, Evas *evas)
{
   Evas_Object *o;
   char buf[4096];

   o = edje_object_add(evas);
   snprintf(buf, sizeof(buf), "%s/e-module-geoclue2.edj",
	    e_module_dir_get(geoclue2_module));
   edje_object_file_set(o, buf, "icon");
   return o;
}

static const char *
_gc_id_new(const E_Gadcon_Client_Class *client_class)
{
   static char buf[4096];

   snprintf(buf, sizeof(buf), "%s.%d", client_class->name,
            eina_list_count(geoclue2_instances) + 1);
   return buf;
}

/* module setup */
EAPI E_Module_Api e_modapi =
{
   E_MODULE_API_VERSION,
     "Geolocation"
};

EAPI void *
e_modapi_init(E_Module *m)
{
   geoclue2_module = m;
   e_gadcon_provider_register(&_gadcon_class);
   return m;
}

EAPI int
e_modapi_shutdown(E_Module *m __UNUSED__)
{
   e_gadcon_provider_unregister(&_gadcon_class);
   return 1;
}

EAPI int
e_modapi_save(E_Module *m __UNUSED__)
{
   return 1;
}
