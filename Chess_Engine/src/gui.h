#ifndef GUI_H
#define GUI_H

#include"globals.h"
#include"gtk/gtk.h"
#include "game.h"
void activate(GtkApplication *app, gpointer user_data);
void open_about_window(GtkWidget *widget, gpointer data);
void open_mode_window(GtkWidget *widget, gpointer data);
void exit_application(GtkWidget *widget, gpointer data);
void open_chessboard(GtkWidget *widget, gpointer data);
void open_pvp_chessboard(GtkWidget *widget, gpointer data);
void go_back_to_choose_mode(GtkWidget *widget, gpointer data);
gboolean draw_border(GtkWidget *widget, cairo_t *cr, gpointer data);
gboolean draw_callback(GtkWidget *widget, cairo_t *cr, gpointer data);
gboolean button_press_event_callback(GtkWidget *widget, GdkEventButton *event, gpointer data);
gboolean pvp_button_press_event_callback(GtkWidget *widget, GdkEventButton *event, gpointer data);
void simulate_button_press(GtkWidget *board_grid_widget, int row, int col);
void show_secondary_window_white();
void show_secondary_window_black();
void show_check_window();
#endif