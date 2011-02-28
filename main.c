#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gdk-pixbuf/gdk-pixdata.h>
#include <gtk/gtk.h>
#include <X11/Xlib.h>
#include <sys/wait.h>
#include <memory.h>
#include <signal.h>
#include <stdio.h>
#include <glib/gi18n.h>
#include <locale.h>

#include "halt.h"
#include "logout.h"
#include "reboot.h"
#include "hibernate.h"

#define PACKAGE "exitx" //软件包名
#define LOCALEDIR "/usr/share/locale" //locale所在目录
//#define _(string)   gettext(string)
//#define N_(string)  string

#define COLOR			"#b6c4d7"
#define BORDER			6

#define	SHUTDOWN_CANCEL		-1
#define	SHUTDOWN_LOGOUT		0
#define SHUTDOWN_HIBERNATE	1
#define SHUTDOWN_REBOOT		2
#define SHUTDOWN_HALT		3

#define LOGOUT_CMD    "/usr/bin/openbox --exit"
#define HIBERNATE_CMD   "dbus-send --system --print-reply --dest='org.freedesktop.UPower' /org/freedesktop/UPower org.freedesktop.UPower.Hibernate"
#define REBOOT_CMD   "dbus-send --system --print-reply --dest='org.freedesktop.ConsoleKit' /org/freedesktop/ConsoleKit/Manager org.freedesktop.ConsoleKit.Manager.Restart"
#define POWEROFF_CMD  "dbus-send --system --print-reply --dest='org.freedesktop.ConsoleKit' /org/freedesktop/ConsoleKit/Manager org.freedesktop.ConsoleKit.Manager.Stop"

typedef struct _XfsmFadeout XfsmFadeout;
typedef struct _FoScreen FoScreen;

struct _FoScreen {
	GdkWindow *window;
	GdkPixmap *backbuf;
};

struct _XfsmFadeout {
	GdkColor color;
	GList *screens;
};

static GtkWidget *dialog = NULL;

static void xfsm_fadeout_drawable_mono(XfsmFadeout * fadeout, GdkDrawable * drawable)
{
	cairo_t *cr;

	/* using Xrender gives better results */
	cr = gdk_cairo_create(drawable);
	gdk_cairo_set_source_color(cr, &fadeout->color);
	cairo_paint_with_alpha(cr, 0.5);
	cairo_destroy(cr);
}

XfsmFadeout *xfsm_fadeout_new(GdkDisplay * display)
{
	GdkWindowAttr attr;
	GdkGCValues values;
	XfsmFadeout *fadeout;
	GdkWindow *root;
	GdkCursor *cursor;
	FoScreen *screen;
	GdkGC *gc;
	GList *lp;
	gint width;
	gint height;
	gint n;

	fadeout = g_new0(XfsmFadeout, 1);
	gdk_color_parse(COLOR, &fadeout->color);

	cursor = gdk_cursor_new(GDK_WATCH);

	attr.x = 0;
	attr.y = 0;
	attr.event_mask = 0;
	attr.wclass = GDK_INPUT_OUTPUT;
	attr.window_type = GDK_WINDOW_TEMP;
	attr.cursor = cursor;
	attr.override_redirect = TRUE;

	for (n = 0; n < gdk_display_get_n_screens(display); ++n) {
		screen = g_new(FoScreen, 1);

		root = gdk_screen_get_root_window(gdk_display_get_screen(display, n));
		gdk_drawable_get_size(GDK_DRAWABLE(root), &width, &height);

		values.function = GDK_COPY;
		values.graphics_exposures = FALSE;
		values.subwindow_mode = TRUE;
		gc = gdk_gc_new_with_values(root, &values, GDK_GC_FUNCTION | GDK_GC_EXPOSURES | GDK_GC_SUBWINDOW);

		screen->backbuf = gdk_pixmap_new(GDK_DRAWABLE(root), width, height, -1);
		gdk_draw_drawable(GDK_DRAWABLE(screen->backbuf), gc, GDK_DRAWABLE(root), 0, 0, 0, 0, width, height);
		xfsm_fadeout_drawable_mono(fadeout, GDK_DRAWABLE(screen->backbuf));

		attr.width = width;
		attr.height = height;

		screen->window = gdk_window_new(root, &attr, GDK_WA_X | GDK_WA_Y | GDK_WA_NOREDIR | GDK_WA_CURSOR);
		gdk_window_set_back_pixmap(screen->window, screen->backbuf, FALSE);

		g_object_unref(G_OBJECT(gc));

		fadeout->screens = g_list_append(fadeout->screens, screen);
	}

	for (lp = fadeout->screens; lp != NULL; lp = lp->next)
		gdk_window_show(((FoScreen *) lp->data)->window);

	gdk_cursor_unref(cursor);

	return fadeout;
}

void xfsm_fadeout_destroy(XfsmFadeout * fadeout)
{
	FoScreen *screen;
	GList *lp;

	for (lp = fadeout->screens; lp != NULL; lp = lp->next) {
		screen = lp->data;

		gdk_window_destroy(screen->window);
		g_object_unref(G_OBJECT(screen->backbuf));
		g_free(screen);
	}

	g_list_free(fadeout->screens);
	g_free(fadeout);
}

static void cancel_button_clicked(GtkWidget * b, gint * shutdownType)
{
	*shutdownType = SHUTDOWN_CANCEL;
	gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
}

static void logout_button_clicked(GtkWidget * b, gint * shutdownType)
{
	*shutdownType = SHUTDOWN_LOGOUT;
	gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
}

static void hibernate_button_clicked(GtkWidget * b, gint * shutdownType)
{
	*shutdownType = SHUTDOWN_HIBERNATE;
	gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
}

static void reboot_button_clicked(GtkWidget * b, gint * shutdownType)
{
	*shutdownType = SHUTDOWN_REBOOT;
	gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
}

static void halt_button_clicked(GtkWidget * b, gint * shutdownType)
{
	*shutdownType = SHUTDOWN_HALT;
	gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
}

void xfce_gtk_window_center_on_monitor(GtkWindow * window, GdkScreen * screen, gint monitor)
{
	GtkRequisition requisition;
	GdkRectangle geometry;
	GdkScreen *widget_screen;
	gint x, y;

	gdk_screen_get_monitor_geometry(screen, monitor, &geometry);

	/* 
	 * Getting a size request requires the widget
	 * to be associated with a screen, because font
	 * information may be needed (Olivier).
	 */
	widget_screen = gtk_widget_get_screen(GTK_WIDGET(window));
	if (screen != widget_screen) {
		gtk_window_set_screen(GTK_WINDOW(window), screen);
	}
	/*
	 * We need to be realized, otherwise we may get 
	 * some odd side effects (Olivier). 
	 */
	if (!GTK_WIDGET_REALIZED(GTK_WIDGET(window))) {
		gtk_widget_realize(GTK_WIDGET(window));
	}
	/*
	 * Yes, I know -1 is useless here (Olivier).
	 */
	requisition.width = requisition.height = -1;
	gtk_widget_size_request(GTK_WIDGET(window), &requisition);

	x = geometry.x + (geometry.width - requisition.width) / 2;
	y = geometry.y + (geometry.height - requisition.height) / 2;

	gtk_window_move(window, x, y);
}

void xfsm_window_add_border(GtkWindow * window)
{
	GtkWidget *box1, *box2;

	gtk_widget_realize(GTK_WIDGET(window));

	box1 = gtk_event_box_new();
	gtk_widget_modify_bg(box1, GTK_STATE_NORMAL, &(GTK_WIDGET(window)->style->bg[GTK_STATE_SELECTED]));
	gtk_widget_show(box1);

	box2 = gtk_event_box_new();
	gtk_widget_show(box2);
	gtk_container_add(GTK_CONTAINER(box1), box2);

	gtk_container_set_border_width(GTK_CONTAINER(box2), 6);
	gtk_widget_reparent(GTK_BIN(window)->child, box2);

	gtk_container_add(GTK_CONTAINER(window), box1);
}

void xfsm_window_grab_input(GtkWindow * window)
{
	GdkWindow *xwindow = GTK_WIDGET(window)->window;

	gdk_pointer_grab(xwindow, TRUE, 0, NULL, NULL, GDK_CURRENT_TIME);
	gdk_keyboard_grab(xwindow, FALSE, GDK_CURRENT_TIME);
	XSetInputFocus(GDK_DISPLAY(), GDK_WINDOW_XWINDOW(xwindow), RevertToParent, CurrentTime);
}

GdkPixbuf *xfce_themed_icon_load(const gchar * name, gint size)
{
	g_return_val_if_fail(name, NULL);

	return gdk_pixbuf_new_from_file_at_size(name, size, size, NULL);
}

static gboolean screen_contains_pointer(GdkScreen * screen, int *x, int *y)
{
	GdkWindow *root_window;
	Window root, child;
	Bool retval;
	int rootx, rooty;
	int winx, winy;
	unsigned int xmask;

	root_window = gdk_screen_get_root_window(screen);

	retval =
	    XQueryPointer(GDK_SCREEN_XDISPLAY(screen), GDK_DRAWABLE_XID(root_window), &root, &child, &rootx, &rooty,
			  &winx, &winy, &xmask);

	if (x)
		*x = retval ? rootx : -1;
	if (y)
		*y = retval ? rooty : -1;

	return retval;
}

GdkScreen *xfce_gdk_display_locate_monitor_with_pointer(GdkDisplay * display, gint * monitor_return)
{
	int n_screens, i;

	if (display == NULL)
		display = gdk_display_get_default();

	n_screens = gdk_display_get_n_screens(display);
	for (i = 0; i < n_screens; i++) {
		GdkScreen *screen;
		int x, y;

		screen = gdk_display_get_screen(display, i);

		if (screen_contains_pointer(screen, &x, &y)) {
			if (monitor_return)
				*monitor_return = gdk_screen_get_monitor_at_point(screen, x, y);

			return screen;
		}
	}

	if (monitor_return)
		*monitor_return = 0;

	return NULL;
}


gboolean main(int argc, char **argv)
{
	XfsmFadeout *fadeout;
	GdkScreen *screen;
	GtkWidget *dbox;
	GtkWidget *hbox;
	GtkWidget *vbox;
	GtkWidget *vbox2;
	GtkWidget *image;
	GtkWidget *hidden;
	GtkWidget *label;
	GtkWidget *logout_button;
	GtkWidget *hibernate_button;
	GtkWidget *reboot_button;
	GtkWidget *halt_button;
	GtkWidget *cancel_button;
	GdkPixbuf *icon;
	gint monitor;
	gint result;
	gint shutdownType;

	gtk_init(&argc, &argv);

    bindtextdomain(PACKAGE, LOCALEDIR);
    //以上函数用来设定国际化翻译包所在位置
    textdomain(PACKAGE);
    //      //以上函数用来设定国际化翻译包名称，省略了.mo
	/* get screen with pointer */
	screen = xfce_gdk_display_locate_monitor_with_pointer(NULL, &monitor);
	if (screen == NULL) {
		screen = gdk_screen_get_default();
		monitor = 0;
	}

	/* Try to grab Input on a hidden window first */
	hidden = gtk_invisible_new_for_screen(screen);
	gtk_widget_show_now(hidden);

	for (;;) {
		if (gdk_pointer_grab(hidden->window, TRUE, 0, NULL, NULL, GDK_CURRENT_TIME) == GDK_GRAB_SUCCESS) {
			if (gdk_keyboard_grab(hidden->window, FALSE, GDK_CURRENT_TIME)
			    == GDK_GRAB_SUCCESS) {
				break;
			}

			gdk_pointer_ungrab(GDK_CURRENT_TIME);
		}

		g_usleep(50 * 1000);
	}

	/* display fadeout */
	fadeout = xfsm_fadeout_new(gtk_widget_get_display(hidden));
	gdk_flush();

	/* create confirm dialog */
	dialog = g_object_new(GTK_TYPE_DIALOG, "type", GTK_WINDOW_POPUP, NULL);
	dialog = dialog;

	gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
	gtk_window_set_screen(GTK_WINDOW(dialog), screen);
	gtk_dialog_set_has_separator(GTK_DIALOG(dialog), FALSE);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);

	dbox = GTK_DIALOG(dialog)->vbox;

	/* - -------------------------------- - vbox */
	vbox = gtk_vbox_new(FALSE, BORDER);
	gtk_box_pack_start(GTK_BOX(dbox), vbox, TRUE, TRUE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), BORDER);
	gtk_widget_show(vbox);

	/* - -------------------------------- - 1st hbox */
	hbox = gtk_hbox_new(TRUE, BORDER);
	gtk_widget_show(hbox);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

	/* - -------------------------------- - */
	/* label */
	//label = gtk_label_new("<b><big>退出系统</big></b>");
	label = gtk_label_new(_("<b><big>Exit System</big></b>"));
	gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
	gtk_widget_show(label);
	gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 0);

	/* - -------------------------------- - 2nd hbox */
	hbox = gtk_hbox_new(TRUE, BORDER);
	gtk_widget_show(hbox);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

	/* - -------------------------------- - */
	/* logout */
	logout_button = gtk_button_new();
	gtk_widget_show(logout_button);
	gtk_box_pack_start(GTK_BOX(hbox), logout_button, TRUE, TRUE, 0);

	g_signal_connect(logout_button, "clicked", G_CALLBACK(logout_button_clicked), &shutdownType);

	vbox2 = gtk_vbox_new(FALSE, BORDER);
	gtk_container_set_border_width(GTK_CONTAINER(vbox2), BORDER);
	gtk_widget_show(vbox2);
	gtk_container_add(GTK_CONTAINER(logout_button), vbox2);

	icon = gdk_pixbuf_from_pixdata(&logout_pixbuf, FALSE, NULL);
	image = gtk_image_new_from_pixbuf(icon);
	gtk_widget_show(image);
	gtk_box_pack_start(GTK_BOX(vbox2), image, FALSE, FALSE, 0);
	g_object_unref(icon);

	//label = gtk_label_new("退出");
	label = gtk_label_new(_("Exit"));
	gtk_widget_show(label);
	gtk_box_pack_start(GTK_BOX(vbox2), label, FALSE, FALSE, 0);

	/* hibernate */
	hibernate_button = gtk_button_new();
	gtk_widget_show(hibernate_button);
	gtk_box_pack_start(GTK_BOX(hbox), hibernate_button, TRUE, TRUE, 0);

	g_signal_connect(hibernate_button, "clicked", G_CALLBACK(hibernate_button_clicked), &shutdownType);

	vbox2 = gtk_vbox_new(FALSE, BORDER);
	gtk_container_set_border_width(GTK_CONTAINER(vbox2), BORDER);
	gtk_widget_show(vbox2);
	gtk_container_add(GTK_CONTAINER(hibernate_button), vbox2);

	icon = gdk_pixbuf_from_pixdata(&hibernate_pixbuf, FALSE, NULL);
	image = gtk_image_new_from_pixbuf(icon);
	gtk_widget_show(image);
	gtk_box_pack_start(GTK_BOX(vbox2), image, FALSE, FALSE, 0);
	g_object_unref(icon);

	//label = gtk_label_new("待机");
	label = gtk_label_new(_("Hibernate"));
	gtk_widget_show(label);
	gtk_box_pack_start(GTK_BOX(vbox2), label, FALSE, FALSE, 0);

	/* reboot */
	reboot_button = gtk_button_new();
	gtk_widget_show(reboot_button);
	gtk_box_pack_start(GTK_BOX(hbox), reboot_button, TRUE, TRUE, 0);

	g_signal_connect(reboot_button, "clicked", G_CALLBACK(reboot_button_clicked), &shutdownType);

	vbox2 = gtk_vbox_new(FALSE, BORDER);
	gtk_container_set_border_width(GTK_CONTAINER(vbox2), BORDER);
	gtk_widget_show(vbox2);
	gtk_container_add(GTK_CONTAINER(reboot_button), vbox2);

	icon = gdk_pixbuf_from_pixdata(&reboot_pixbuf, FALSE, NULL);
	image = gtk_image_new_from_pixbuf(icon);
	gtk_widget_show(image);
	gtk_box_pack_start(GTK_BOX(vbox2), image, FALSE, FALSE, 0);
	g_object_unref(icon);

	//label = gtk_label_new("重启");
	label = gtk_label_new(_("Reboot"));
	gtk_widget_show(label);
	gtk_box_pack_start(GTK_BOX(vbox2), label, FALSE, FALSE, 0);

	/* halt */
	halt_button = gtk_button_new();
	gtk_widget_show(halt_button);
	gtk_box_pack_start(GTK_BOX(hbox), halt_button, TRUE, TRUE, 0);

	g_signal_connect(halt_button, "clicked", G_CALLBACK(halt_button_clicked), &shutdownType);

	vbox2 = gtk_vbox_new(FALSE, BORDER);
	gtk_container_set_border_width(GTK_CONTAINER(vbox2), BORDER);
	gtk_widget_show(vbox2);
	gtk_container_add(GTK_CONTAINER(halt_button), vbox2);

	icon = gdk_pixbuf_from_pixdata(&halt_pixbuf, FALSE, NULL);
	image = gtk_image_new_from_pixbuf(icon);
	gtk_widget_show(image);
	gtk_box_pack_start(GTK_BOX(vbox2), image, FALSE, FALSE, 0);
	g_object_unref(icon);

	//label = gtk_label_new("关机");
	label = gtk_label_new(_("Poweroff"));
	gtk_widget_show(label);
	gtk_box_pack_start(GTK_BOX(vbox2), label, FALSE, FALSE, 0);

	/* - -------------------------------- - 3rd hbox */
	hbox = gtk_hbox_new(TRUE, BORDER);
	gtk_widget_show(hbox);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

	vbox2 = gtk_vbox_new(TRUE, BORDER);
	gtk_widget_show(vbox2);
	gtk_box_pack_start(GTK_BOX(hbox), vbox2, TRUE, TRUE, 0);

	vbox2 = gtk_vbox_new(TRUE, BORDER);
	gtk_widget_show(vbox2);
	gtk_box_pack_start(GTK_BOX(hbox), vbox2, TRUE, TRUE, 0);

	vbox2 = gtk_vbox_new(TRUE, BORDER);
	gtk_widget_show(vbox2);
	gtk_box_pack_start(GTK_BOX(hbox), vbox2, TRUE, TRUE, 0);

	//cancel_button = gtk_button_new_with_label("取消");
	cancel_button = gtk_button_new_with_label(_("Cancel"));
	g_signal_connect(cancel_button, "clicked", G_CALLBACK(cancel_button_clicked), &shutdownType);
	gtk_widget_show(cancel_button);
	gtk_box_pack_start(GTK_BOX(hbox), cancel_button, TRUE, TRUE, 0);

	/* create small border */
	xfsm_window_add_border(GTK_WINDOW(dialog));

	/* center dialog on target monitor */
	xfce_gtk_window_center_on_monitor(GTK_WINDOW(dialog), screen, monitor);

	gtk_widget_grab_focus(GTK_WIDGET(cancel_button));
	/* connect to the shutdown helper */
	/* save portion of the root window covered by the dialog */
	/* need to realize the dialog first! */
	gtk_widget_show_now(dialog);

	/* Grab Keyboard and Mouse pointer */
	xfsm_window_grab_input(GTK_WINDOW(dialog));

	/* run the logout dialog */
	result = gtk_dialog_run(GTK_DIALOG(dialog));

	gtk_widget_hide(dialog);

	gtk_widget_destroy(dialog);
	gtk_widget_destroy(hidden);

	dialog = NULL;
	/* Release Keyboard/Mouse pointer grab */
	xfsm_fadeout_destroy(fadeout);

	gdk_pointer_ungrab(GDK_CURRENT_TIME);
	gdk_keyboard_ungrab(GDK_CURRENT_TIME);
	gdk_flush();

	/* process all pending events first */
	while (gtk_events_pending())
		g_main_context_iteration(NULL, FALSE);

	switch (shutdownType) {
	case SHUTDOWN_CANCEL:
		break;
	case SHUTDOWN_LOGOUT:
		system(LOGOUT_CMD);
		break;
	case SHUTDOWN_HIBERNATE:
		system(HIBERNATE_CMD);
		break;
	case SHUTDOWN_REBOOT:
		system(REBOOT_CMD);
		break;
	case SHUTDOWN_HALT:
		system(POWEROFF_CMD);
		break;
	default:
		break;
	}
	return (result == GTK_RESPONSE_OK);
}

// vim:encoding=utf8
