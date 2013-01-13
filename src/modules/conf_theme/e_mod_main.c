#include "e.h"
#include "e_mod_main.h"

static const char *cur_theme = NULL;

static E_Module *conf_module = NULL;
static E_Int_Menu_Augmentation *maug[8] = {0};

/* module setup */
EAPI E_Module_Api e_modapi =
{
   E_MODULE_API_VERSION,
   "Settings - Theme"
};

/* menu item add hook */
static void
_e_mod_run_wallpaper_cb(void *data __UNUSED__, E_Menu *m, E_Menu_Item *mi __UNUSED__)
{
   e_configure_registry_call("appearance/wallpaper", m->zone->container, NULL);
}

static void
_e_mod_menu_wallpaper_add(void *data __UNUSED__, E_Menu *m)
{
   E_Menu_Item *mi;

   mi = e_menu_item_new(m);
   e_menu_item_label_set(mi, _("Wallpaper"));
   e_util_menu_item_theme_icon_set(mi, "preferences-desktop-wallpaper");
   e_menu_item_callback_set(mi, _e_mod_run_wallpaper_cb, NULL);
}

static void
_e_mod_run_theme_cb(void *data __UNUSED__, E_Menu *m, E_Menu_Item *mi __UNUSED__)
{
   e_configure_registry_call("appearance/theme", m->zone->container, NULL);
}

static void
_theme_set(void *data, E_Menu *m __UNUSED__, E_Menu_Item *mi __UNUSED__)
{
   E_Action *a;

   e_theme_config_set("theme", data);
   e_config_save_queue();

   a = e_action_find("restart");
   if ((a) && (a->func.go)) a->func.go(NULL, NULL);
}

static void
_item_new(Eina_Stringshare *file, E_Menu *m)
{
   E_Menu_Item *mi;
   char *name, *sfx;
   Eina_Bool used;

   name = (char*)ecore_file_file_get(file);
   if (!name) return;
   used = (!e_util_strcmp(name, cur_theme));
   sfx = strrchr(name, '.');
   name = strndupa(name, sfx - name);
   mi = e_menu_item_new(m);
   e_menu_item_label_set(mi, name);
   if (used)
     e_menu_item_disabled_set(mi, 1);
   else
     e_menu_item_callback_set(mi, _theme_set, file);
   e_menu_item_radio_group_set(mi, 1);
   e_menu_item_radio_set(mi, 1);
   e_menu_item_toggle_set(mi, used);
}

static void
_e_mod_menu_theme_del(void *d __UNUSED__)
{
   cur_theme = NULL;
}

static void
_e_mod_menu_theme_add(void *data __UNUSED__, E_Menu *m)
{
   E_Menu_Item *mi;
   E_Config_Theme *ct;
   Eina_Stringshare *file;
   const Eina_List *themes, *sthemes, *l;

   mi = e_menu_item_new(m);
   e_menu_item_label_set(mi, _("Theme"));
   e_util_menu_item_theme_icon_set(mi, "preferences-desktop-theme");
   e_menu_item_callback_set(mi, _e_mod_run_theme_cb, NULL);
   ct = e_theme_config_get("theme");
   if (!ct) return;

   cur_theme = (char*)ecore_file_file_get(ct->file);   
   m = e_menu_new();
   e_object_del_attach_func_set(E_OBJECT(m), _e_mod_menu_theme_del);
   e_menu_title_set(m, "Themes");
   e_menu_item_submenu_set(mi, m);
   e_object_unref(E_OBJECT(m));

   themes = e_configure_option_util_themes_get();
   sthemes = e_configure_option_util_themes_system_get();
   EINA_LIST_FOREACH(themes, l, file)
     _item_new(file, m);
   if (themes && sthemes)
     e_menu_item_separator_set(e_menu_item_new(m), 1);
   EINA_LIST_FOREACH(sthemes, l, file)
     _item_new(file, m);
}

EAPI void *
e_modapi_init(E_Module *m)
{
   e_configure_registry_category_add("internal", -1, _("Internal"),
                                     NULL, "enlightenment/internal");
   e_configure_registry_item_add("internal/wallpaper_desk", -1, _("Wallpaper"),
                                 NULL, "preferences-system-windows", e_int_config_wallpaper_desk);
   e_configure_registry_item_add("internal/borders_border", -1, _("Border"),
                                 NULL, "preferences-system-windows", e_int_config_borders_border);

   e_configure_registry_category_add("appearance", 10, _("Look"), NULL,
                                     "preferences-look");
   e_configure_registry_item_add("appearance/wallpaper", 10, _("Wallpaper"), NULL,
                                 "preferences-desktop-wallpaper",
                                 e_int_config_wallpaper);
   e_configure_registry_item_add("appearance/theme", 20, _("Theme"), NULL,
                                 "preferences-desktop-theme",
                                 e_int_config_theme);
   e_configure_registry_item_add("appearance/xsettings", 20, _("Application Theme"), NULL,
                                 "preferences-desktop-theme",
                                 e_int_config_xsettings);
   e_configure_registry_item_add("appearance/colors", 30, _("Colors"), NULL,
                                 "preferences-desktop-color",
                                 e_int_config_color_classes);
   e_configure_registry_item_add("appearance/fonts", 40, _("Fonts"), NULL,
                                 "preferences-desktop-font",
                                 e_int_config_fonts);
   e_configure_registry_item_add("appearance/borders", 50, _("Borders"), NULL,
                                 "preferences-system-windows",
                                 e_int_config_borders);
   e_configure_registry_item_add("appearance/transitions", 60, _("Transitions"), NULL,
                                 "preferences-transitions",
                                 e_int_config_transitions);
   e_configure_registry_item_add("appearance/scale", 70, _("Scaling"), NULL,
                                 "preferences-scale",
                                 e_int_config_scale);
   e_configure_registry_item_add("appearance/startup", 80, _("Startup"), NULL,
                                 "preferences-startup",
                                 e_int_config_startup);

   maug[0] =
     e_int_menus_menu_augmentation_add_sorted("config/1", _("Wallpaper"),
                                              _e_mod_menu_wallpaper_add, NULL, NULL, NULL);
   maug[1] =
     e_int_menus_menu_augmentation_add_sorted("config/1", _("Theme"),
                                              _e_mod_menu_theme_add, NULL, NULL, NULL);

   conf_module = m;
   e_module_delayed_set(m, 1);

   return m;
}

EAPI int
e_modapi_shutdown(E_Module *m __UNUSED__)
{
   E_Config_Dialog *cfd;

   /* remove module-supplied menu additions */
   if (maug[0])
     {
        e_int_menus_menu_augmentation_del("config/1", maug[0]);
        maug[0] = NULL;
     }
   if (maug[1])
     {
        e_int_menus_menu_augmentation_del("config/1", maug[1]);
        maug[1] = NULL;
     }

   cur_theme = NULL;

   while ((cfd = e_config_dialog_get("E", "appearance/startup")))
     e_object_del(E_OBJECT(cfd));
   while ((cfd = e_config_dialog_get("E", "appearance/scale")))
     e_object_del(E_OBJECT(cfd));
   while ((cfd = e_config_dialog_get("E", "appearance/transitions")))
     e_object_del(E_OBJECT(cfd));
   while ((cfd = e_config_dialog_get("E", "appearance/borders")))
     e_object_del(E_OBJECT(cfd));
   while ((cfd = e_config_dialog_get("E", "appearance/fonts")))
     e_object_del(E_OBJECT(cfd));
   while ((cfd = e_config_dialog_get("E", "appearance/colors")))
     e_object_del(E_OBJECT(cfd));
   while ((cfd = e_config_dialog_get("E", "apppearance/theme")))
     e_object_del(E_OBJECT(cfd));
   while ((cfd = e_config_dialog_get("E", "appearance/wallpaper")))
     e_object_del(E_OBJECT(cfd));
   while ((cfd = e_config_dialog_get("E", "appearance/xsettings")))
     e_object_del(E_OBJECT(cfd));

   e_configure_registry_item_del("appearance/startup");
   e_configure_registry_item_del("appearance/scale");
   e_configure_registry_item_del("appearance/transitions");
   e_configure_registry_item_del("appearance/borders");
   e_configure_registry_item_del("appearance/fonts");
   e_configure_registry_item_del("appearance/colors");
   e_configure_registry_item_del("appearance/theme");
   e_configure_registry_item_del("appearance/wallpaper");
   e_configure_registry_item_del("appearance/xsettings");
   e_configure_registry_category_del("appearance");

   while ((cfd = e_config_dialog_get("E", "internal/borders_border")))
     e_object_del(E_OBJECT(cfd));
   while ((cfd = e_config_dialog_get("E", "appearance/wallpaper")))
     e_object_del(E_OBJECT(cfd));
   e_configure_registry_item_del("appearance/borders");
   e_configure_registry_item_del("internal/wallpaper_desk");
   e_configure_registry_category_del("internal");

   conf_module = NULL;
   return 1;
}
