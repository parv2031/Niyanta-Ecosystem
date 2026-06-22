#include"globals.h"
#include"gtk/gtk.h"
#include "gui.h"
#include "game.h"
// Function to open the About window
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

int screen_w = 1920;
int screen_h = 1080;
int SQUARE_SIZE = 100;
int BOARD_OFFSET_X = 50;
int BOARD_OFFSET_Y = 50;

static GtkWidget *main_menu_window = NULL;
static GtkWidget *main_fixed_container = NULL;
static GtkWidget *button_mode_main = NULL;
static GtkWidget *button_about_main = NULL;
static GtkWidget *button_exit_main = NULL;

static GtkWidget *button_pvp = NULL;
static GtkWidget *button_ai = NULL;
static GtkWidget *button_back_to_main = NULL;
static GtkApplication *main_app = NULL;

static GtkWidget *board_fixed_container = NULL;

void handle_undo_clicked(GtkWidget *widget, gpointer data) {
    if (!history_empty()) {
        undo_last_move();
    }
}

void handle_redo_clicked(GtkWidget *widget, gpointer data) {
    redo_last_move();
}

void get_asset_path(const char* relative_path, char* resolved_path, size_t max_len) {
    char path1[256];
    char path2[256];
    snprintf(path1, sizeof(path1), "../src/assets/%s", relative_path);
    snprintf(path2, sizeof(path2), "src/assets/%s", relative_path);
    
    if (access(path1, F_OK) == 0) {
        strncpy(resolved_path, path1, max_len);
    } else if (access(path2, F_OK) == 0) {
        strncpy(resolved_path, path2, max_len);
    } else {
        // Default to the first one if neither exists, GTK will show broken icon
        strncpy(resolved_path, path1, max_len);
    }
}
static GtkWidget *help_button_overlay = NULL;

void hide_about_overlay(GtkWidget *widget, gpointer data) {
    if (help_button_overlay) {
        gtk_widget_hide(help_button_overlay);
    }
}

void open_about_window(GtkWidget *widget, gpointer data) {
    if (!help_button_overlay) {
        char resolved_path[256];
        get_asset_path("Chessrules1.png", resolved_path, sizeof(resolved_path));
        GtkWidget *image = gtk_image_new_from_file(resolved_path);
        
        help_button_overlay = gtk_button_new();
        gtk_button_set_image(GTK_BUTTON(help_button_overlay), image);
        gtk_button_set_relief(GTK_BUTTON(help_button_overlay), GTK_RELIEF_NONE);
        
        g_signal_connect(help_button_overlay, "clicked", G_CALLBACK(hide_about_overlay), NULL);
        
        int w = 800, h = 600;
        GdkPixbuf *rules_pixbuf = gdk_pixbuf_new_from_file(resolved_path, NULL);
        if (rules_pixbuf) {
            w = gdk_pixbuf_get_width(rules_pixbuf);
            h = gdk_pixbuf_get_height(rules_pixbuf);
            g_object_unref(rules_pixbuf);
        }
        gtk_fixed_put(GTK_FIXED(main_fixed_container), help_button_overlay, (screen_w - w) / 2, (screen_h - h) / 2);
    }
    gtk_widget_show_all(help_button_overlay);
}

// Function to exit the application
void exit_application(GtkWidget *widget, gpointer data) {
    GtkWidget *window = (GtkWidget *)data;
    gtk_widget_destroy(window);
    gtk_main_quit();
}
void go_back_to_main_menu(GtkWidget *widget, gpointer data) {
    if (button_pvp) gtk_widget_hide(button_pvp);
    if (button_ai) gtk_widget_hide(button_ai);
    if (button_back_to_main) gtk_widget_hide(button_back_to_main);

    if (button_mode_main) gtk_widget_show(button_mode_main);
    if (button_about_main) gtk_widget_show(button_about_main);
    if (button_exit_main) gtk_widget_show(button_exit_main);
}

// Function to open the Mode window (now in-place)
void open_mode_window(GtkWidget *widget, gpointer data) {
    if (button_mode_main) gtk_widget_hide(button_mode_main);
    if (button_about_main) gtk_widget_hide(button_about_main);
    if (button_exit_main) gtk_widget_hide(button_exit_main);

    if (!button_pvp) {
        button_pvp = gtk_button_new_with_label("PvP");
        gtk_widget_set_size_request(button_pvp, 200, 80);
        g_signal_connect(button_pvp, "clicked", G_CALLBACK(open_pvp_chessboard), main_app);
        gtk_fixed_put(GTK_FIXED(main_fixed_container), button_pvp, (screen_w / 2) - 250, screen_h / 2);
        gtk_widget_set_name(button_pvp, "glow_button");

        button_ai = gtk_button_new_with_label("vs Computer");
        gtk_widget_set_size_request(button_ai, 200, 80);
        g_signal_connect(button_ai, "clicked", G_CALLBACK(open_chessboard), main_app);
        gtk_fixed_put(GTK_FIXED(main_fixed_container), button_ai, (screen_w / 2) + 50, screen_h / 2);
        gtk_widget_set_name(button_ai, "glow_button");

        button_back_to_main = gtk_button_new_with_label("Back");
        gtk_widget_set_size_request(button_back_to_main, 100, 40);
        g_signal_connect(button_back_to_main, "clicked", G_CALLBACK(go_back_to_main_menu), NULL);
        gtk_fixed_put(GTK_FIXED(main_fixed_container), button_back_to_main, 50, 50);
        gtk_widget_set_name(button_back_to_main, "oval_button");

        // CSS for these is handled globally
    }

    gtk_widget_show(button_pvp);
    gtk_widget_show(button_ai);
    gtk_widget_show(button_back_to_main);
}

// Activation function for the initial window
void activate(GtkApplication *app, gpointer user_data) {
    main_app = app;
    button_pvp = NULL;
    button_ai = NULL;
    button_back_to_main = NULL;
    help_button_overlay = NULL;

    GtkWidget *window;
    GtkWidget *fixed;
    GtkWidget *button_exit;
    GtkWidget *button_about;
    GtkWidget *button_mode;
    GtkWidget *background_image;

    GdkDisplay *display = gdk_display_get_default();
    GdkMonitor *monitor = gdk_display_get_primary_monitor(display);
    if (!monitor) {
        monitor = gdk_display_get_monitor(display, 0);
    }
    
    if (monitor) {
        GdkRectangle workarea;
        gdk_monitor_get_geometry(monitor, &workarea);
        screen_w = workarea.width;
        screen_h = workarea.height;
    } else {
        screen_w = 1920;
        screen_h = 1080;
    }
    SQUARE_SIZE = (screen_h - 150) / 8;
    BOARD_OFFSET_X = (screen_w - (SQUARE_SIZE * 8)) / 2;
    BOARD_OFFSET_Y = (screen_h - (SQUARE_SIZE * 8)) / 2;

    window = gtk_application_window_new(app);
    main_menu_window = window;
    gtk_window_set_title(GTK_WINDOW(window), "CHECKMATE");
    gtk_window_fullscreen(GTK_WINDOW(window));

    fixed = gtk_fixed_new();
    main_fixed_container = fixed;
    gtk_container_add(GTK_CONTAINER(window), fixed);
    g_signal_connect(window, "destroy", G_CALLBACK(reset_board), NULL);

    // Load and add the background image
    char resolved_bg_path[256];
    get_asset_path("chess2.png", resolved_bg_path, sizeof(resolved_bg_path));
    GdkPixbuf *bg_pixbuf = gdk_pixbuf_new_from_file(resolved_bg_path, NULL);
    if (bg_pixbuf) {
        GdkPixbuf *bg_scaled = gdk_pixbuf_scale_simple(bg_pixbuf, screen_w, screen_h, GDK_INTERP_BILINEAR);
        background_image = gtk_image_new_from_pixbuf(bg_scaled);
        g_object_unref(bg_pixbuf);
        g_object_unref(bg_scaled);
    } else {
        background_image = gtk_image_new_from_file(resolved_bg_path);
    }
    gtk_fixed_put(GTK_FIXED(fixed), background_image, 0, 0);

    // Create About button
    button_about = gtk_button_new_with_label("HI ! NEED HELP ?");
    button_about_main = button_about;
    g_signal_connect(button_about, "clicked", G_CALLBACK(open_about_window), app);
    gtk_fixed_put(GTK_FIXED(fixed), button_about, (screen_w - 300) / 2, 50);
    gtk_widget_set_name(button_about, "ovaLl_button");

    // Create Exit button
    button_exit = gtk_button_new_with_label("Exit");
    button_exit_main = button_exit;
    g_signal_connect(button_exit, "clicked", G_CALLBACK(exit_application), window);
    gtk_fixed_put(GTK_FIXED(fixed), button_exit, 50, screen_h - 100);
    gtk_widget_set_name(button_exit, "ovLal_button");

    // Create Mode button
    button_mode = gtk_button_new_with_label("PLAY");
    button_mode_main = button_mode;
    g_signal_connect(button_mode, "clicked", G_CALLBACK(open_mode_window), app);
    gtk_fixed_put(GTK_FIXED(fixed), button_mode, screen_w - 200, screen_h - 100);
    gtk_widget_set_name(button_mode, "oval_button");

    gtk_widget_show_all(window);
    // Apply CSS styling globally
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider,
        "button {"
        "  font-family: 'Inter', 'Segoe UI', sans-serif;"
        "  font-weight: bold;"
        "  font-size: 20px;"
        "  color: white;"
        "  text-shadow: 1px 1px 3px rgba(0,0,0,0.5);"
        "  box-shadow: 0px 4px 10px rgba(0,0,0,0.5);"
        "  background-color: #444;"
        "  transition: all 0.2s ease-in-out;"
        "}"
        "button:hover {"
        "  box-shadow: 0px 6px 15px rgba(255,215,0,0.4);"
        "  opacity: 0.9;"
        "}"
        "#oval_button, #ovLal_button {"
        "  background-image: linear-gradient(to bottom, #E82A45, #9B0017);"
        "  border-radius: 30px;"
        "  padding: 15px 40px;"
        "  border: 2px solid #5A000A;"
        "}"
        "#ovaLl_button {"
        "  background-image: linear-gradient(to bottom, #FFDF33, #CCAA00);"
        "  color: #333;"
        "  text-shadow: none;"
        "  border-radius: 40px;"
        "  padding: 15px 60px;"
        "  border: 2px solid #887000;"
        "}"
        "#action_button {"
        "  background-image: linear-gradient(to bottom, #2B3A42, #1C262B);"
        "  border-radius: 12px;"
        "  border: 2px solid #111;"
        "}"
        "#glow_button {"
        "  font-size: 24px;"
        "  color: #000;"
        "  text-shadow: none;"
        "  background-color: #FFD700;"
        "  background-image: none;"
        "  border-radius: 10px;"
        "  border: 2px solid #FFD700;"
        "  box-shadow: 0px 0px 10px 2px #FFD700;"
        "  transition: box-shadow 0.3s ease-in-out;"
        "}"
        "#glow_button:hover {"
        "  box-shadow: 0px 0px 20px 5px #FFD700;"
        "}"
        ,
        -1, NULL);

    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}
void open_chessboard(GtkWidget *widget, gpointer data) {
    reset_board();
    if (main_menu_window) {
        gtk_widget_destroy(main_menu_window);
        main_menu_window = NULL;
    }
    GtkApplication *app = GTK_APPLICATION(data);
    GtkWidget *window;
    GtkWidget *fixed;
    GtkWidget *background_area;
    // GtkWidget *grid;
    GtkWidget *drawing_area;
    GtkWidget *back_button;
    BoardSquareData *button_data;
     GtkWidget *background_image;

    int i, j;

    window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "vs Computer Mode");
    gtk_window_fullscreen(GTK_WINDOW(window));

    fixed = gtk_fixed_new();
    board_fixed_container = fixed;
    gtk_container_add(GTK_CONTAINER(window), fixed);
    g_signal_connect(window, "destroy", G_CALLBACK(reset_board), NULL);

    // Load and add the background image
    char resolved_bg_path[256];
    get_asset_path("chess3.png", resolved_bg_path, sizeof(resolved_bg_path));
    GdkPixbuf *bg_pixbuf = gdk_pixbuf_new_from_file(resolved_bg_path, NULL);
    if (bg_pixbuf) {
        GdkPixbuf *bg_scaled = gdk_pixbuf_scale_simple(bg_pixbuf, screen_w, screen_h, GDK_INTERP_BILINEAR);
        background_image = gtk_image_new_from_pixbuf(bg_scaled);
        g_object_unref(bg_pixbuf);
        g_object_unref(bg_scaled);
    } else {
        background_image = gtk_image_new_from_file(resolved_bg_path);
    }
    gtk_fixed_put(GTK_FIXED(fixed), background_image, 0, 0);
   
    board_grid_widget = gtk_grid_new();

    // Draw dark brown border around chessboard
    GtkWidget *border_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(border_area, screen_w, screen_h);
    g_signal_connect(border_area, "draw", G_CALLBACK(draw_border), NULL);
    gtk_fixed_put(GTK_FIXED(fixed), border_area, 0, 0);

    // Create 64 drawing areas (8x8 board_grid_widget)
    BoardSquareData *button_data_grid[8][8]; // To store button data for each board_grid_widget cell
    for (i = 0; i < 8; i++) {
        for (j = 0; j < 8; j++) {
            drawing_area = gtk_drawing_area_new();
            gtk_widget_set_size_request(drawing_area, SQUARE_SIZE, SQUARE_SIZE);
            button_data = g_malloc(sizeof(BoardSquareData));
            button_data->row = i;
            button_data->col = j;
            button_data->is_pressed = FALSE;
            // GtkWidget *button=gtk_button_new();
            // Initialize piece widget with NULL
            button_data->piece_widget = NULL;
            // button_data->button=button;

            button_data_grid[i][j] = button_data; // Store button data in grid

            g_signal_connect(G_OBJECT(drawing_area), "draw", G_CALLBACK(draw_callback), button_data);
            g_signal_connect(G_OBJECT(drawing_area), "button-press-event", G_CALLBACK(button_press_event_callback), button_data);
            gtk_widget_set_events(drawing_area, GDK_BUTTON_PRESS_MASK);
            gtk_grid_attach(GTK_GRID(board_grid_widget), drawing_area, j, i, 1, 1);
            // gtk_grid_attach(GTK_GRID(board_grid_widget), button, j, i, 1, 1);
            
        }
    }

    // Add the board_grid_widget to the fixed container at a specific position
    gtk_fixed_put(GTK_FIXED(fixed), board_grid_widget, BOARD_OFFSET_X, BOARD_OFFSET_Y); // Moves the board_grid_widget to (50, 50) within the window

    // Adding chess pieces at their initial positions
    // Path to the chess pieces images
    char piece_rel_path[256];
    char filepath[256];
// Load and place chess pieces
    for (i = 0; i < 8; i++) {
        for (j = 0; j < 8; j++) {
            if (strlen(chess_board[i][j]) > 0) {
                snprintf(piece_rel_path, sizeof(piece_rel_path), "chess_pieces/%s", chess_board[i][j]);
                get_asset_path(piece_rel_path, filepath, sizeof(filepath));
                GdkPixbuf *piece_pixbuf = gdk_pixbuf_new_from_file_at_size(filepath, SQUARE_SIZE, SQUARE_SIZE, NULL);
                GtkWidget *image = NULL;
                if (piece_pixbuf) {
                    image = gtk_image_new_from_pixbuf(piece_pixbuf);
                    g_object_unref(piece_pixbuf);
                } else {
                    image = gtk_image_new_from_file(filepath);
                }
                if (!image) {
                    g_print("Failed to load image: %s\n", filepath);
                } else {
                    gtk_fixed_put(GTK_FIXED(fixed), image, BOARD_OFFSET_X + j * SQUARE_SIZE, BOARD_OFFSET_Y + i * SQUARE_SIZE);
                    // Update button data to reference the piece image
                    button_data_grid[i][j]->piece_widget = image;
                }
            }
        }
    }

    // Add a "Back" button to return to the "Choose Mode" window
    back_button = gtk_button_new_with_label("Back");
    g_signal_connect(back_button, "clicked", G_CALLBACK(go_back_to_choose_mode), window);
    gtk_fixed_put(GTK_FIXED(fixed), back_button, 50, 50);
    gtk_widget_set_size_request(back_button, 120, 50);
    gtk_widget_set_name(back_button, "action_button");

    // Add Undo button
    GtkWidget *undo_button = gtk_button_new_with_label("Undo Move");
    g_signal_connect(undo_button, "clicked", G_CALLBACK(handle_undo_clicked), NULL);
    gtk_fixed_put(GTK_FIXED(fixed), undo_button, screen_w - 200, 50);
    gtk_widget_set_size_request(undo_button, 150, 50);
    gtk_widget_set_name(undo_button, "action_button");

    // Add Redo button
    GtkWidget *redo_button = gtk_button_new_with_label("Redo Move");
    g_signal_connect(redo_button, "clicked", G_CALLBACK(handle_redo_clicked), NULL);
    gtk_fixed_put(GTK_FIXED(fixed), redo_button, screen_w - 200, 110);
    gtk_widget_set_size_request(redo_button, 150, 50);
    gtk_widget_set_name(redo_button, "action_button");

    // Show all widgets in the window
    gtk_widget_show_all(window);

    for (GList *l = captured_pieces_list; l != NULL; l = l->next) {
    CapturedPiece *removed_piece = (CapturedPiece *)l->data;
    gtk_widget_show(removed_piece->piece_widget);
    button_data_grid[removed_piece->row][removed_piece->col]->piece_widget = removed_piece->piece_widget;
    g_free(removed_piece);
}
g_list_free(captured_pieces_list);
captured_pieces_list = NULL;
}
void open_pvp_chessboard(GtkWidget *widget, gpointer data) {
    reset_board();
    if (main_menu_window) {
        gtk_widget_destroy(main_menu_window);
        main_menu_window = NULL;
    }
    GtkApplication *app = GTK_APPLICATION(data);
    GtkWidget *window;
    GtkWidget *fixed;
    GtkWidget *background_area;
    GtkWidget *grid;
    GtkWidget *drawing_area;
    GtkWidget *back_button;
    BoardSquareData *button_data;
    GtkWidget *background_image;
    int i, j;

    window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "PvP Mode");
    gtk_window_fullscreen(GTK_WINDOW(window));

    fixed = gtk_fixed_new();
    board_fixed_container = fixed;
    gtk_container_add(GTK_CONTAINER(window), fixed);
    g_signal_connect(window, "destroy", G_CALLBACK(reset_board), NULL);

    // Load and add the background image
    char resolved_bg_path[256];
    get_asset_path("chess3.png", resolved_bg_path, sizeof(resolved_bg_path));
    GdkPixbuf *bg_pixbuf = gdk_pixbuf_new_from_file(resolved_bg_path, NULL);
    if (bg_pixbuf) {
        GdkPixbuf *bg_scaled = gdk_pixbuf_scale_simple(bg_pixbuf, screen_w, screen_h, GDK_INTERP_BILINEAR);
        background_image = gtk_image_new_from_pixbuf(bg_scaled);
        g_object_unref(bg_pixbuf);
        g_object_unref(bg_scaled);
    } else {
        background_image = gtk_image_new_from_file(resolved_bg_path);
    }
    gtk_fixed_put(GTK_FIXED(fixed), background_image, 0, 0);
   
    board_grid_widget = gtk_grid_new();

    // Draw dark brown border around chessboard
    GtkWidget *border_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(border_area, screen_w, screen_h);
    g_signal_connect(border_area, "draw", G_CALLBACK(draw_border), NULL);
    gtk_fixed_put(GTK_FIXED(fixed), border_area, 0, 0);

    // Create 64 drawing areas (8x8 board_grid_widget)
    BoardSquareData *button_data_grid[8][8]; // To store button data for each board_grid_widget cell
    for (i = 0; i < 8; i++) {
        for (j = 0; j < 8; j++) {
            drawing_area = gtk_drawing_area_new();
            gtk_widget_set_size_request(drawing_area, SQUARE_SIZE, SQUARE_SIZE);
            button_data = g_malloc(sizeof(BoardSquareData));
            button_data->row = i;
            button_data->col = j;
            button_data->is_pressed = FALSE;

            // Initialize piece widget with NULL
            button_data->piece_widget = NULL;

            button_data_grid[i][j] = button_data; // Store button data in grid

            g_signal_connect(G_OBJECT(drawing_area), "draw", G_CALLBACK(draw_callback), button_data);
            g_signal_connect(G_OBJECT(drawing_area), "button-press-event", G_CALLBACK(pvp_button_press_event_callback), button_data);
            gtk_widget_set_events(drawing_area, GDK_BUTTON_PRESS_MASK);
            gtk_grid_attach(GTK_GRID(board_grid_widget), drawing_area, j, i, 1, 1);
        }
    }

    // Add the board_grid_widget to the fixed container at a specific position
    gtk_fixed_put(GTK_FIXED(fixed), board_grid_widget, BOARD_OFFSET_X, BOARD_OFFSET_Y); // Moves the board_grid_widget to (50, 50) within the window

    // Adding chess pieces at their initial positions
    // Path to the chess pieces images
    char piece_rel_path[256];
    char filepath[256];
// Load and place chess pieces
    for (i = 0; i < 8; i++) {
        for (j = 0; j < 8; j++) {
            if (strlen(chess_board[i][j]) > 0) {
                snprintf(piece_rel_path, sizeof(piece_rel_path), "chess_pieces/%s", chess_board[i][j]);
                get_asset_path(piece_rel_path, filepath, sizeof(filepath));
                GdkPixbuf *piece_pixbuf = gdk_pixbuf_new_from_file_at_size(filepath, SQUARE_SIZE, SQUARE_SIZE, NULL);
                GtkWidget *image = NULL;
                if (piece_pixbuf) {
                    image = gtk_image_new_from_pixbuf(piece_pixbuf);
                    g_object_unref(piece_pixbuf);
                } else {
                    image = gtk_image_new_from_file(filepath);
                }
                if (!image) {
                    g_print("Failed to load image: %s\n", filepath);
                } else {
                    gtk_fixed_put(GTK_FIXED(fixed), image, BOARD_OFFSET_X + j * SQUARE_SIZE, BOARD_OFFSET_Y + i * SQUARE_SIZE);
                    // Update button data to reference the piece image
                    button_data_grid[i][j]->piece_widget = image;
                }
            }
        }
    }

    // Add a "Back" button to return to the "Choose Mode" window
    back_button = gtk_button_new_with_label("Back");
    g_signal_connect(back_button, "clicked", G_CALLBACK(go_back_to_choose_mode), window);
    gtk_fixed_put(GTK_FIXED(fixed), back_button, 50, 50);
    gtk_widget_set_size_request(back_button, 120, 50);
    gtk_widget_set_name(back_button, "action_button");

    // Add Undo button
    GtkWidget *undo_button_pvp = gtk_button_new_with_label("Undo Move");
    g_signal_connect(undo_button_pvp, "clicked", G_CALLBACK(handle_undo_clicked), NULL);
    gtk_fixed_put(GTK_FIXED(fixed), undo_button_pvp, screen_w - 200, 50);
    gtk_widget_set_size_request(undo_button_pvp, 150, 50);
    gtk_widget_set_name(undo_button_pvp, "action_button");

    // Add Redo button
    GtkWidget *redo_button_pvp = gtk_button_new_with_label("Redo Move");
    g_signal_connect(redo_button_pvp, "clicked", G_CALLBACK(handle_redo_clicked), NULL);
    gtk_fixed_put(GTK_FIXED(fixed), redo_button_pvp, screen_w - 200, 110);
    gtk_widget_set_size_request(redo_button_pvp, 150, 50);
    gtk_widget_set_name(redo_button_pvp, "action_button");

    // Show all widgets in the window
    gtk_widget_show_all(window);

    for (GList *l = captured_pieces_list; l != NULL; l = l->next) {
    CapturedPiece *removed_piece = (CapturedPiece *)l->data;
    gtk_widget_show(removed_piece->piece_widget);
    button_data_grid[removed_piece->row][removed_piece->col]->piece_widget = removed_piece->piece_widget;
    g_free(removed_piece);
}
g_list_free(captured_pieces_list);
captured_pieces_list = NULL;
}
void go_back_to_choose_mode(GtkWidget *widget, gpointer data) {
    reset_board();
    GtkWidget *window = (GtkWidget *)data;
    gtk_widget_destroy(window);
    activate(main_app, NULL);
}
gboolean draw_border(GtkWidget *widget, cairo_t *cr, gpointer data) {
    // Set the color for the border (dark brown)
    cairo_set_source_rgb(cr, 0.36, 0.25, 0.20); // RGB values for dark brown color
    cairo_set_line_width(cr, 30); // Border width
    cairo_rectangle(cr, BOARD_OFFSET_X - 10, BOARD_OFFSET_Y - 10, SQUARE_SIZE * 8 + 20, SQUARE_SIZE * 8 + 20);
    cairo_stroke(cr); // Apply the stroke
    return FALSE;
}
gboolean draw_callback(GtkWidget *widget, cairo_t *cr, gpointer data) {
    BoardSquareData *button_data = (BoardSquareData *)data;
    // Set color based on the position and press state
    if (button_data->is_pressed) {
        if ((button_data->row + button_data->col) % 2==0) {
            cairo_set_source_rgb(cr, 0, 1, 0); // purplish

        } else {
            cairo_set_source_rgb(cr, 0, 1, 0); // purplish

        }
    } else if ((button_data->row + button_data->col) % 2==0) {
        cairo_set_source_rgb(cr, .55, .62, .45); // white
    } else {
        cairo_set_source_rgb(cr, .24, .62, .32); // light black
    }
    cairo_paint(cr); // Fill the entire area with the color
    // Check if the current button is in the possible_moves array and draw a circle if so
    for (int i = 0; i < num_possible_moves; i++) {
        if (possible_moves[i][0] == button_data->row && possible_moves[i][1] == button_data->col) {
            if (button_data->piece_widget) {
                cairo_set_source_rgb(cr, 1, 0, 0); // red for squares with a piece
                cairo_arc(cr, SQUARE_SIZE / 2, SQUARE_SIZE / 2, SQUARE_SIZE * 5 / 12, 0, 2 * G_PI); // Draw a circle of radius 24px at the center (30, 30) of the square
            } else {
                cairo_set_source_rgb(cr, 0, 0, 1); // blue for empty squares
                cairo_arc(cr, SQUARE_SIZE / 2, SQUARE_SIZE / 2, SQUARE_SIZE / 5, 0, 2 * G_PI); // Draw a circle of radius 24px at the center (30, 30) of the square
            }
            
            cairo_fill(cr);
            break; // Exit the loop once the button is found in the possible_moves array
        }
    }
    
    // Check if the current button is in the possible_moves array and draw a circle if so
    // for (int i = 0; i < num_possible_moves; i++) {
    //     if (possible_moves[i][0] == button_data->row && possible_moves[i][1] == button_data->col) {
    //         cairo_set_source_rgb(cr, 255, 0, 0); // Purplish color for the circle
    //         cairo_arc(cr, SQUARE_SIZE / 2, SQUARE_SIZE / 2, SQUARE_SIZE * 2 / 5, 0, 2 * G_PI); // Draw a circle of radius 10px at the center (30, 30) of the square
    //         cairo_fill(cr);
    //         break; // Exit the loop once the button is found in the possible_moves array
    //     }
    // }

    return FALSE; // Return FALSE to indicate no further drawing is needed
}
void simulate_button_press(GtkWidget *grid, int row, int col) {
    GtkWidget *widget = gtk_grid_get_child_at(GTK_GRID(board_grid_widget), col, row);
    if (!widget) {
        g_print("No widget found at (%d, %d)\n", row, col);
        return;
    }
    GdkEvent *event = gdk_event_new(GDK_BUTTON_PRESS);
    event->button.type = GDK_BUTTON_PRESS;
    event->button.window = gtk_widget_get_window(widget);
    g_object_ref(event->button.window);
    event->button.send_event = TRUE;
    event->button.time = GDK_CURRENT_TIME;
    event->button.x = 1.0;
    event->button.y = 1.0;
    event->button.axes = NULL;
    event->button.state = 0;
    event->button.button = 1;
    event->button.device = gdk_seat_get_pointer(gdk_display_get_default_seat(gdk_display_get_default()));
    event->button.x_root = 1.0;
    event->button.y_root = 1.0;
    gdk_event_set_device(event, event->button.device);

    gtk_widget_event(widget, event);
    gdk_event_free(event);

    gtk_widget_queue_draw(widget);
    gtk_widget_queue_draw(board_grid_widget);
}
static GtkWidget *checkmate_overlay_button = NULL;
void hide_checkmate_overlay(GtkWidget *widget, gpointer data) {
    if (checkmate_overlay_button) {
        gtk_widget_destroy(checkmate_overlay_button);
        checkmate_overlay_button = NULL;
    }
}

void show_secondary_window_white() {
    if (!checkmate_overlay_button && board_fixed_container) {
        char resolved_path[256];
        get_asset_path("checkmate_white_win.PNG", resolved_path, sizeof(resolved_path));
        GtkWidget *image = gtk_image_new_from_file(resolved_path);
        
        checkmate_overlay_button = gtk_button_new();
        gtk_button_set_image(GTK_BUTTON(checkmate_overlay_button), image);
        gtk_button_set_relief(GTK_BUTTON(checkmate_overlay_button), GTK_RELIEF_NONE);
        
        g_signal_connect(checkmate_overlay_button, "clicked", G_CALLBACK(hide_checkmate_overlay), NULL);
        
        int w = 400, h = 200;
        GdkPixbuf *rules_pixbuf = gdk_pixbuf_new_from_file(resolved_path, NULL);
        if (rules_pixbuf) {
            w = gdk_pixbuf_get_width(rules_pixbuf);
            h = gdk_pixbuf_get_height(rules_pixbuf);
            g_object_unref(rules_pixbuf);
        }
        gtk_fixed_put(GTK_FIXED(board_fixed_container), checkmate_overlay_button, (screen_w - w) / 2, (screen_h - h) / 2);
        gtk_widget_show_all(checkmate_overlay_button);
    }
}

void show_secondary_window_black() {
    if (!checkmate_overlay_button && board_fixed_container) {
        char resolved_path[256];
        get_asset_path("Checkmate_black_win.png", resolved_path, sizeof(resolved_path));
        GtkWidget *image = gtk_image_new_from_file(resolved_path);
        
        checkmate_overlay_button = gtk_button_new();
        gtk_button_set_image(GTK_BUTTON(checkmate_overlay_button), image);
        gtk_button_set_relief(GTK_BUTTON(checkmate_overlay_button), GTK_RELIEF_NONE);
        
        g_signal_connect(checkmate_overlay_button, "clicked", G_CALLBACK(hide_checkmate_overlay), NULL);
        
        int w = 400, h = 200;
        GdkPixbuf *rules_pixbuf = gdk_pixbuf_new_from_file(resolved_path, NULL);
        if (rules_pixbuf) {
            w = gdk_pixbuf_get_width(rules_pixbuf);
            h = gdk_pixbuf_get_height(rules_pixbuf);
            g_object_unref(rules_pixbuf);
        }
        gtk_fixed_put(GTK_FIXED(board_fixed_container), checkmate_overlay_button, (screen_w - w) / 2, (screen_h - h) / 2);
        gtk_widget_show_all(checkmate_overlay_button);
    }
}

void show_stalemate_window() {
    if (!checkmate_overlay_button && board_fixed_container) {
        char resolved_path[256];
        get_asset_path("stalemate.png", resolved_path, sizeof(resolved_path));
        GtkWidget *image = gtk_image_new_from_file(resolved_path);
        
        checkmate_overlay_button = gtk_button_new();
        gtk_button_set_image(GTK_BUTTON(checkmate_overlay_button), image);
        gtk_button_set_relief(GTK_BUTTON(checkmate_overlay_button), GTK_RELIEF_NONE);
        
        g_signal_connect(checkmate_overlay_button, "clicked", G_CALLBACK(hide_checkmate_overlay), NULL);
        
        int w = 400, h = 200;
        GdkPixbuf *rules_pixbuf = gdk_pixbuf_new_from_file(resolved_path, NULL);
        if (rules_pixbuf) {
            w = gdk_pixbuf_get_width(rules_pixbuf);
            h = gdk_pixbuf_get_height(rules_pixbuf);
            g_object_unref(rules_pixbuf);
        }
        gtk_fixed_put(GTK_FIXED(board_fixed_container), checkmate_overlay_button, (screen_w - w) / 2, (screen_h - h) / 2);
        gtk_widget_show_all(checkmate_overlay_button);
    }
}

static GtkWidget *check_overlay_button = NULL;
void hide_check_overlay(GtkWidget *widget, gpointer data) {
    if (check_overlay_button) {
        gtk_widget_destroy(check_overlay_button);
        check_overlay_button = NULL;
    }
}

void show_check_window() {
    if (!check_overlay_button && board_fixed_container) {
        check_overlay_button = gtk_button_new_with_label("CHECK!!");
        GtkCssProvider *provider = gtk_css_provider_new();
        gtk_css_provider_load_from_data(provider,
            "button {"
            "  font-size: 60px;"
            "  font-weight: bold;"
            "  color: white;"
            "  background-color: rgba(255, 0, 0, 0.8);"
            "  border-radius: 20px;"
            "}", -1, NULL);
        gtk_style_context_add_provider(gtk_widget_get_style_context(check_overlay_button), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
        
        g_signal_connect(check_overlay_button, "clicked", G_CALLBACK(hide_check_overlay), NULL);
        gtk_widget_set_size_request(check_overlay_button, 400, 200);
        gtk_fixed_put(GTK_FIXED(board_fixed_container), check_overlay_button, (screen_w - 400) / 2, (screen_h - 200) / 2);
        gtk_widget_show_all(check_overlay_button);
    }
}
gboolean button_press_event_callback(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    BoardSquareData *button_data = (BoardSquareData *)data;
    g_print("Button at (%d, %d) pressed\n", button_data->row, button_data->col);

    char* ppiece = chess_board[button_data->row][button_data->col];
    if (strlen(ppiece) > 0) {
        g_print("%s\n\n", ppiece);
    } else {
        g_print("empty\n\n");
    }
    if(current_turn==WHITE_TURN){
        if (selected_square == button_data) {
            // Deselect the piece if clicked again
            selected_square->is_pressed = FALSE;
            selected_square = NULL;
            g_print("Deselected piece at (%d, %d)\n", button_data->row, button_data->col);
            // Clear possible moves to remove circles
            num_possible_moves = 0;
            GtkWidget *grid = gtk_widget_get_parent(widget);
            gtk_widget_queue_draw(board_grid_widget);
        } else if (!selected_square && button_data->piece_widget) {
            // Check if it's the correct turn for this piece
            gboolean valid_turn = FALSE;
            if (current_turn == WHITE_TURN && (strcmp(ppiece, WHITE_PAWN) == 0 || strcmp(ppiece, WHITE_ROOK) == 0 || strcmp(ppiece, WHITE_KNIGHT) == 0 || strcmp(ppiece, WHITE_BISHOP) == 0 || strcmp(ppiece, WHITE_QUEEN) == 0 || strcmp(ppiece, WHITE_KING) == 0)) {
                valid_turn = TRUE;
            }
           if(valid_turn){
            // If this button has a piece, select it
            selected_square = button_data;
            if (strcmp(chess_board[button_data->row][button_data->col], WHITE_PAWN) == 0) {
                pawn_moves(button_data->row, button_data->col, chess_board, current_turn == WHITE_TURN ? COLOR_WHITE : COLOR_BLACK);
                filter_invalid_moves(button_data->row, button_data->col,chess_board, current_turn == WHITE_TURN ? COLOR_WHITE : COLOR_BLACK);
            }
            if (strcmp(chess_board[button_data->row][button_data->col], WHITE_ROOK) == 0) {
                rook_moves(button_data->row, button_data->col, chess_board, current_turn == WHITE_TURN ? COLOR_WHITE : COLOR_BLACK);
                filter_invalid_moves(button_data->row, button_data->col,chess_board, current_turn == WHITE_TURN ? COLOR_WHITE : COLOR_BLACK);
            }
            if (strcmp(chess_board[button_data->row][button_data->col], WHITE_KNIGHT) == 0) {
                knight_moves(button_data->row, button_data->col, chess_board, current_turn == WHITE_TURN ? COLOR_WHITE : COLOR_BLACK);
                filter_invalid_moves(button_data->row, button_data->col,chess_board, current_turn == WHITE_TURN ? COLOR_WHITE : COLOR_BLACK);
            }
            if (strcmp(chess_board[button_data->row][button_data->col], WHITE_BISHOP) == 0) {
                bishop_moves(button_data->row, button_data->col, chess_board, current_turn == WHITE_TURN ? COLOR_WHITE : COLOR_BLACK);
                filter_invalid_moves(button_data->row, button_data->col,chess_board, current_turn == WHITE_TURN ? COLOR_WHITE : COLOR_BLACK);
            }
            if (strcmp(chess_board[button_data->row][button_data->col], WHITE_QUEEN) == 0) {
                queen_moves(button_data->row, button_data->col, chess_board, current_turn == WHITE_TURN ? COLOR_WHITE : COLOR_BLACK);
                filter_invalid_moves(button_data->row, button_data->col,chess_board, current_turn == WHITE_TURN ? COLOR_WHITE : COLOR_BLACK);
            }
            if (strcmp(chess_board[button_data->row][button_data->col], WHITE_KING) == 0) {
                king_moves(button_data->row, button_data->col, chess_board, current_turn == WHITE_TURN ? COLOR_WHITE : COLOR_BLACK);
                filter_invalid_moves(button_data->row, button_data->col, chess_board, current_turn == WHITE_TURN ? COLOR_WHITE : COLOR_BLACK);
            }
            button_data->is_pressed = TRUE;
            GtkWidget *grid = gtk_widget_get_parent(widget);
            gtk_widget_queue_draw(board_grid_widget); // Redraw the entire board_grid_widget to show circles immediately
            
        }} else if (selected_square) {
            // Check if the target position is in the possible moves
            gboolean valid_move = FALSE;
            for (int i = 0; i < num_possible_moves; i++) {
                if (possible_moves[i][0] == button_data->row && possible_moves[i][1] == button_data->col) {
                    valid_move = TRUE;
                    break;
                }
            }
            // if(is_checkmate(chess_board, current_turn == WHITE_TURN ? COLOR_BLACK : COLOR_WHITE)==0) valid_move=FALSE;

            if (valid_move) {
                // Record the move before mutating state
                MoveRecord m;
                m.from_r = selected_square->row;
                m.from_c = selected_square->col;
                m.to_r = button_data->row;
                m.to_c = button_data->col;
                m.moved_piece = chess_board[selected_square->row][selected_square->col];
                m.captured_piece = chess_board[button_data->row][button_data->col];
                m.moved_widget = selected_square->piece_widget;
                m.captured_widget = button_data->piece_widget;
                m.from_square = selected_square;
                m.to_square = button_data;
                push_move(m);

                // If there's a selected piece and this button is occupied, remove the existing piece temporarily
                if (button_data->piece_widget) {
                    CapturedPiece *removed_piece = g_malloc(sizeof(CapturedPiece));
                    removed_piece->piece_widget = button_data->piece_widget;
                    removed_piece->row = button_data->row;
                    removed_piece->col = button_data->col;
                    captured_pieces_list = g_list_prepend(captured_pieces_list, removed_piece);
                    gtk_widget_hide(button_data->piece_widget);
                    button_data->piece_widget = NULL;
                }
                // Move the piece
                gtk_fixed_move(GTK_FIXED(gtk_widget_get_parent(selected_square->piece_widget)), selected_square->piece_widget, BOARD_OFFSET_X + button_data->col * SQUARE_SIZE, BOARD_OFFSET_Y + button_data->row * SQUARE_SIZE);
                button_data->piece_widget = selected_square->piece_widget;
                // Update the chess_board array
                chess_board[button_data->row][button_data->col] = chess_board[selected_square->row][selected_square->col]; // Set the new position to the moved piece name
                chess_board[selected_square->row][selected_square->col] = ""; // Set the original position to empty

                // Pawn Promotion
                if (strcmp(chess_board[button_data->row][button_data->col], WHITE_PAWN) == 0 && button_data->row == 0) {
                    chess_board[button_data->row][button_data->col] = WHITE_QUEEN;
                    char path[256];
                    get_asset_path("chess_pieces/wq.png", path, sizeof(path));
                    gtk_image_set_from_file(GTK_IMAGE(button_data->piece_widget), path);
                }

                selected_square->piece_widget = NULL;
                selected_square->is_pressed = FALSE;
                selected_square = NULL;
                num_possible_moves = 0;
                GtkWidget *grid = gtk_widget_get_parent(widget);
                gtk_widget_queue_draw(board_grid_widget); 
                int z = 1;
                Color opp_color = (current_turn == WHITE_TURN) ? COLOR_BLACK : COLOR_WHITE;
                if(is_checkmate(chess_board, opp_color)==1){
                    if(is_in_check(chess_board, opp_color)==1) {
                        show_secondary_window_black();
                    } else {
                        show_stalemate_window();
                    }
                    z=0;
                } else if(is_in_check(chess_board, opp_color)==1 && z!=0) {
                    show_check_window();
                }
                current_turn=BLACK_TURN;
            }
            if(current_turn==BLACK_TURN){
                    ai_random(chess_board);
                    GtkWidget *grid = gtk_widget_get_parent(widget);
                    simulate_button_press(board_grid_widget,bot_src_row,bot_src_col);
                    gtk_widget_queue_draw(board_grid_widget);
                    num_possible_moves=0;
                    simulate_button_press(board_grid_widget,bot_dest[0],bot_dest[1]);
                    gtk_widget_queue_draw(board_grid_widget);
                    bot_used_capture=1;
                    
            }

            current_turn=WHITE_TURN;
        }
        return TRUE;
    }
    if(current_turn==BLACK_TURN){
        if (selected_square == button_data) {
            // Deselect the piece if clicked again
            selected_square->is_pressed = FALSE;
            selected_square = NULL;
            g_print("Deselected piece at (%d, %d)\n", button_data->row, button_data->col);
            // Clear possible moves to remove circles
            num_possible_moves = 0;
            GtkWidget *grid = gtk_widget_get_parent(widget);
            gtk_widget_queue_draw(board_grid_widget);
        } else if (!selected_square && button_data->piece_widget) {
            // If this button has a piece, select it
            selected_square = button_data;
            if(bot_used_capture!=0){
                if (strcmp(chess_board[button_data->row][button_data->col], BLACK_PAWN) == 0) {
                    pawn_moves(button_data->row, button_data->col, chess_board, current_turn == WHITE_TURN ? COLOR_WHITE : COLOR_BLACK);
                    filter_invalid_moves(button_data->row,  button_data->col, chess_board, current_turn == WHITE_TURN ? COLOR_WHITE : COLOR_BLACK);
                    button_data->is_pressed = TRUE;
                    GtkWidget *grid = gtk_widget_get_parent(widget);
                    gtk_widget_queue_draw(board_grid_widget);
                    random_move(num_possible_moves,possible_moves);
                }
                if (strcmp(chess_board[button_data->row][button_data->col], BLACK_ROOK) == 0) {
                    rook_moves(button_data->row, button_data->col, chess_board, current_turn == WHITE_TURN ? COLOR_WHITE : COLOR_BLACK);
                    filter_invalid_moves(button_data->row,  button_data->col, chess_board, current_turn == WHITE_TURN ? COLOR_WHITE : COLOR_BLACK);
                    button_data->is_pressed = TRUE;
                    GtkWidget *grid = gtk_widget_get_parent(widget);
                    gtk_widget_queue_draw(board_grid_widget);
                    random_move(num_possible_moves,possible_moves);
                }
                if (strcmp(chess_board[button_data->row][button_data->col], BLACK_KNIGHT) == 0) {
                    knight_moves(button_data->row, button_data->col, chess_board, current_turn == WHITE_TURN ? COLOR_WHITE : COLOR_BLACK);
                    filter_invalid_moves(button_data->row,  button_data->col, chess_board, current_turn == WHITE_TURN ? COLOR_WHITE : COLOR_BLACK);
                    button_data->is_pressed = TRUE;
                    GtkWidget *grid = gtk_widget_get_parent(widget);
                    gtk_widget_queue_draw(board_grid_widget);
                    random_move(num_possible_moves,possible_moves);
                }
                if (strcmp(chess_board[button_data->row][button_data->col], BLACK_BISHOP) == 0) {
                    bishop_moves(button_data->row, button_data->col, chess_board, current_turn == WHITE_TURN ? COLOR_WHITE : COLOR_BLACK);
                    filter_invalid_moves(button_data->row,  button_data->col, chess_board, current_turn == WHITE_TURN ? COLOR_WHITE : COLOR_BLACK);
                    button_data->is_pressed = TRUE;
                    GtkWidget *grid = gtk_widget_get_parent(widget);
                    gtk_widget_queue_draw(board_grid_widget);
                    random_move(num_possible_moves,possible_moves);
                }
                if (strcmp(chess_board[button_data->row][button_data->col], BLACK_QUEEN) == 0) {
                    queen_moves(button_data->row, button_data->col, chess_board, current_turn == WHITE_TURN ? COLOR_WHITE : COLOR_BLACK);
                    filter_invalid_moves(button_data->row,  button_data->col, chess_board, current_turn == WHITE_TURN ? COLOR_WHITE : COLOR_BLACK);
                    button_data->is_pressed = TRUE;
                    GtkWidget *grid = gtk_widget_get_parent(widget);
                    gtk_widget_queue_draw(board_grid_widget);
                    random_move(num_possible_moves,possible_moves);
                }
                if (strcmp(chess_board[button_data->row][button_data->col], BLACK_KING) == 0) {
                    king_moves(button_data->row, button_data->col, chess_board, current_turn == WHITE_TURN ? COLOR_WHITE : COLOR_BLACK);
                    filter_invalid_moves(button_data->row,  button_data->col, chess_board, current_turn == WHITE_TURN ? COLOR_WHITE : COLOR_BLACK);
                    button_data->is_pressed = TRUE;
                    GtkWidget *grid = gtk_widget_get_parent(widget);
                    gtk_widget_queue_draw(board_grid_widget);
                    random_move(num_possible_moves,possible_moves);
                }
            }
        } else if (selected_square) {
                // Record the bot's move before mutating state
                MoveRecord m;
                m.from_r = selected_square->row;
                m.from_c = selected_square->col;
                m.to_r = button_data->row;
                m.to_c = button_data->col;
                m.moved_piece = chess_board[selected_square->row][selected_square->col];
                m.captured_piece = chess_board[button_data->row][button_data->col];
                m.moved_widget = selected_square->piece_widget;
                m.captured_widget = button_data->piece_widget;
                m.from_square = selected_square;
                m.to_square = button_data;
                push_move(m);

                if (button_data->piece_widget) {
                    CapturedPiece *removed_piece = g_malloc(sizeof(CapturedPiece));
                    removed_piece->piece_widget = button_data->piece_widget;
                    removed_piece->row = button_data->row;
                    removed_piece->col = button_data->col;
                    captured_pieces_list = g_list_prepend(captured_pieces_list, removed_piece);
                    gtk_widget_hide(button_data->piece_widget);
                    button_data->piece_widget = NULL;
                }
                // Move the piece
                gtk_fixed_move(GTK_FIXED(gtk_widget_get_parent(selected_square->piece_widget)), selected_square->piece_widget, BOARD_OFFSET_X + button_data->col * SQUARE_SIZE, BOARD_OFFSET_Y + button_data->row * SQUARE_SIZE);
                button_data->piece_widget = selected_square->piece_widget;
                // Update the chess_board array
                chess_board[button_data->row][button_data->col] = chess_board[selected_square->row][selected_square->col]; // Set the new position to the moved piece name
                chess_board[selected_square->row][selected_square->col] = ""; // Set the original position to empty

                // Pawn Promotion (Bot)
                if (strcmp(chess_board[button_data->row][button_data->col], BLACK_PAWN) == 0 && button_data->row == 7) {
                    chess_board[button_data->row][button_data->col] = BLACK_QUEEN;
                    char path[256];
                    get_asset_path("chess_pieces/bq.png", path, sizeof(path));
                    gtk_image_set_from_file(GTK_IMAGE(button_data->piece_widget), path);
                }

                selected_square->piece_widget = NULL;
                selected_square->is_pressed = FALSE;
                selected_square = NULL;
                num_possible_moves = 0; 
                GtkWidget *grid = gtk_widget_get_parent(widget);
                gtk_widget_queue_draw(board_grid_widget);
                int z = 1;
                Color opp_color = (current_turn == WHITE_TURN) ? COLOR_BLACK : COLOR_WHITE;
                if(is_checkmate(chess_board, opp_color)==1){
                    if(is_in_check(chess_board, opp_color)==1) {
                        show_secondary_window_white();
                    } else {
                        show_stalemate_window();
                    }
                    z=0;
                } else if(is_in_check(chess_board, opp_color)==1 && z!=0) {
                    show_check_window();
                }
        }
        return TRUE;
    }
}
gboolean pvp_button_press_event_callback(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    BoardSquareData *button_data = (BoardSquareData *)data;
    g_print("Button at (%d, %d) pressed\n", button_data->row, button_data->col);

    char* ppiece = chess_board[button_data->row][button_data->col];
    if (strlen(ppiece) > 0) {
        g_print("%s\n\n", ppiece);
    } else {
        g_print("empty\n\n");
    }

    if (selected_square == button_data) {
        // Deselect the piece if clicked again
        selected_square->is_pressed = FALSE;
        selected_square = NULL;
        g_print("Deselected piece at (%d, %d)\n", button_data->row, button_data->col);
        // Clear possible moves to remove circles
        num_possible_moves = 0;
        GtkWidget *grid = gtk_widget_get_parent(widget);
        gtk_widget_queue_draw(board_grid_widget);
    } else if (!selected_square && button_data->piece_widget) {
        // Check if it's the correct turn for this piece
        gboolean valid_turn = FALSE;
        if (current_turn == WHITE_TURN && (strcmp(ppiece, WHITE_PAWN) == 0 || strcmp(ppiece, WHITE_ROOK) == 0 || strcmp(ppiece, WHITE_KNIGHT) == 0 || strcmp(ppiece, WHITE_BISHOP) == 0 || strcmp(ppiece, WHITE_QUEEN) == 0 || strcmp(ppiece, WHITE_KING) == 0)) {
            valid_turn = TRUE;
        } else if (current_turn == BLACK_TURN && (strcmp(ppiece, BLACK_PAWN) == 0 || strcmp(ppiece, BLACK_ROOK) == 0 || strcmp(ppiece, BLACK_KNIGHT) == 0 || strcmp(ppiece, BLACK_BISHOP) == 0 || strcmp(ppiece, BLACK_QUEEN) == 0 || strcmp(ppiece, BLACK_KING) == 0)) {
            valid_turn = TRUE;
        }
       if(valid_turn){
        // If this button has a piece, select it
        selected_square = button_data;
        if (strcmp(chess_board[button_data->row][button_data->col], WHITE_PAWN) == 0) {
            pawn_moves(button_data->row, button_data->col, chess_board, current_turn == WHITE_TURN ? COLOR_WHITE : COLOR_BLACK);
            filter_invalid_moves(button_data->row, button_data->col,chess_board, current_turn == WHITE_TURN ? COLOR_WHITE : COLOR_BLACK);
        }
        if (strcmp(chess_board[button_data->row][button_data->col], BLACK_PAWN) == 0) {
            pawn_moves(button_data->row, button_data->col, chess_board, current_turn == WHITE_TURN ? COLOR_WHITE : COLOR_BLACK);
            filter_invalid_moves(button_data->row, button_data->col,chess_board, current_turn == WHITE_TURN ? COLOR_WHITE : COLOR_BLACK);
        }
        if (strcmp(chess_board[button_data->row][button_data->col], BLACK_ROOK) == 0) {
            rook_moves(button_data->row, button_data->col, chess_board, current_turn == WHITE_TURN ? COLOR_WHITE : COLOR_BLACK);
            filter_invalid_moves(button_data->row, button_data->col,chess_board, current_turn == WHITE_TURN ? COLOR_WHITE : COLOR_BLACK);
        }
        if (strcmp(chess_board[button_data->row][button_data->col], WHITE_ROOK) == 0) {
            rook_moves(button_data->row, button_data->col, chess_board, current_turn == WHITE_TURN ? COLOR_WHITE : COLOR_BLACK);
            filter_invalid_moves(button_data->row, button_data->col,chess_board, current_turn == WHITE_TURN ? COLOR_WHITE : COLOR_BLACK);
        }
        if (strcmp(chess_board[button_data->row][button_data->col], WHITE_KNIGHT) == 0) {
            knight_moves(button_data->row, button_data->col, chess_board, current_turn == WHITE_TURN ? COLOR_WHITE : COLOR_BLACK);
            filter_invalid_moves(button_data->row, button_data->col,chess_board, current_turn == WHITE_TURN ? COLOR_WHITE : COLOR_BLACK);
        }
        if (strcmp(chess_board[button_data->row][button_data->col], BLACK_KNIGHT) == 0) {
            knight_moves(button_data->row, button_data->col, chess_board, current_turn == WHITE_TURN ? COLOR_WHITE : COLOR_BLACK);
            filter_invalid_moves(button_data->row, button_data->col,chess_board, current_turn == WHITE_TURN ? COLOR_WHITE : COLOR_BLACK);
        }
        if (strcmp(chess_board[button_data->row][button_data->col], WHITE_BISHOP) == 0) {
            bishop_moves(button_data->row, button_data->col, chess_board, current_turn == WHITE_TURN ? COLOR_WHITE : COLOR_BLACK);
            filter_invalid_moves(button_data->row, button_data->col,chess_board, current_turn == WHITE_TURN ? COLOR_WHITE : COLOR_BLACK);
        }
        if (strcmp(chess_board[button_data->row][button_data->col], BLACK_BISHOP) == 0) {
            bishop_moves(button_data->row, button_data->col, chess_board, current_turn == WHITE_TURN ? COLOR_WHITE : COLOR_BLACK);
            filter_invalid_moves(button_data->row, button_data->col,chess_board, current_turn == WHITE_TURN ? COLOR_WHITE : COLOR_BLACK);
        }
        if (strcmp(chess_board[button_data->row][button_data->col], WHITE_QUEEN) == 0) {
            queen_moves(button_data->row, button_data->col, chess_board, current_turn == WHITE_TURN ? COLOR_WHITE : COLOR_BLACK);
            filter_invalid_moves(button_data->row, button_data->col,chess_board, current_turn == WHITE_TURN ? COLOR_WHITE : COLOR_BLACK);
        }
        if (strcmp(chess_board[button_data->row][button_data->col], BLACK_QUEEN) == 0) {
            queen_moves(button_data->row, button_data->col, chess_board, current_turn == WHITE_TURN ? COLOR_WHITE : COLOR_BLACK);
            filter_invalid_moves(button_data->row, button_data->col,chess_board, current_turn == WHITE_TURN ? COLOR_WHITE : COLOR_BLACK);
        }
        if (strcmp(chess_board[button_data->row][button_data->col], WHITE_KING) == 0) {
            king_moves(button_data->row, button_data->col, chess_board, current_turn == WHITE_TURN ? COLOR_WHITE : COLOR_BLACK);
            filter_invalid_moves(button_data->row, button_data->col,chess_board, current_turn == WHITE_TURN ? COLOR_WHITE : COLOR_BLACK);
        }
        if (strcmp(chess_board[button_data->row][button_data->col], BLACK_KING) == 0) {
            king_moves(button_data->row, button_data->col, chess_board, current_turn == WHITE_TURN ? COLOR_WHITE : COLOR_BLACK);
            filter_invalid_moves(button_data->row, button_data->col, chess_board, current_turn == WHITE_TURN ? COLOR_WHITE : COLOR_BLACK);
        }
        button_data->is_pressed = TRUE;
        // if(is_checkmate(chess_board, current_turn == WHITE_TURN ? COLOR_BLACK : COLOR_WHITE)!=0){
        GtkWidget *grid = gtk_widget_get_parent(widget);
        gtk_widget_queue_draw(board_grid_widget); // Redraw the entire board_grid_widget to show circles immediately
        // }
        // else printf("check");
    }} else if (selected_square) {
        // Check if the target position is in the possible moves
        gboolean valid_move = FALSE;
        for (int i = 0; i < num_possible_moves; i++) {
            if (possible_moves[i][0] == button_data->row && possible_moves[i][1] == button_data->col) {
                valid_move = TRUE;
                break;
            }
        }
        // if(is_checkmate(chess_board, current_turn == WHITE_TURN ? COLOR_BLACK : COLOR_WHITE)==0) valid_move=FALSE;

        if (valid_move) {
            // Record the move before mutating state
            MoveRecord m;
            m.from_r = selected_square->row;
            m.from_c = selected_square->col;
            m.to_r = button_data->row;
            m.to_c = button_data->col;
            m.moved_piece = chess_board[selected_square->row][selected_square->col];
            m.captured_piece = chess_board[button_data->row][button_data->col];
            m.moved_widget = selected_square->piece_widget;
            m.captured_widget = button_data->piece_widget;
            m.from_square = selected_square;
            m.to_square = button_data;
            push_move(m);

            // If there's a selected piece and this button is occupied, remove the existing piece temporarily
            if (button_data->piece_widget) {
                CapturedPiece *removed_piece = g_malloc(sizeof(CapturedPiece));
                removed_piece->piece_widget = button_data->piece_widget;
                removed_piece->row = button_data->row;
                removed_piece->col = button_data->col;
                captured_pieces_list = g_list_prepend(captured_pieces_list, removed_piece);
                gtk_widget_hide(button_data->piece_widget);
                button_data->piece_widget = NULL;
            }
            // Move the piece
            gtk_fixed_move(GTK_FIXED(gtk_widget_get_parent(selected_square->piece_widget)), selected_square->piece_widget, BOARD_OFFSET_X + button_data->col * SQUARE_SIZE, BOARD_OFFSET_Y + button_data->row * SQUARE_SIZE);
            button_data->piece_widget = selected_square->piece_widget;
            // Update the chess_board array
            chess_board[button_data->row][button_data->col] = chess_board[selected_square->row][selected_square->col]; // Set the new position to the moved piece name
            chess_board[selected_square->row][selected_square->col] = ""; // Set the original position to empty
            
            // Pawn Promotion (PvP)
            if (strcmp(chess_board[button_data->row][button_data->col], WHITE_PAWN) == 0 && button_data->row == 0) {
                chess_board[button_data->row][button_data->col] = WHITE_QUEEN;
                char path[256];
                get_asset_path("chess_pieces/wq.png", path, sizeof(path));
                gtk_image_set_from_file(GTK_IMAGE(button_data->piece_widget), path);
            } else if (strcmp(chess_board[button_data->row][button_data->col], BLACK_PAWN) == 0 && button_data->row == 7) {
                chess_board[button_data->row][button_data->col] = BLACK_QUEEN;
                char path[256];
                get_asset_path("chess_pieces/bq.png", path, sizeof(path));
                gtk_image_set_from_file(GTK_IMAGE(button_data->piece_widget), path);
            }

            selected_square->piece_widget = NULL;
            selected_square->is_pressed = FALSE;
            selected_square = NULL;
            num_possible_moves = 0; // Clear possible moves to remove circles
            GtkWidget *grid = gtk_widget_get_parent(widget);
            int z = 1;
            Color opp_color = (current_turn == WHITE_TURN) ? COLOR_BLACK : COLOR_WHITE;
            if(is_checkmate(chess_board, opp_color)==1){
                if(is_in_check(chess_board, opp_color)==1) {
                    if (current_turn == WHITE_TURN) show_secondary_window_black();
                    else show_secondary_window_white();
                } else {
                    show_stalemate_window();
                }
                z=0;
            } else if(is_in_check(chess_board, opp_color)==1 && z!=0) {
                show_check_window();
            }
            // Switch turn
            gtk_widget_queue_draw(board_grid_widget); // Redraw the entire board_grid_widget to show circles immediately
            current_turn = (current_turn == WHITE_TURN) ? BLACK_TURN : WHITE_TURN;
        }
    }
    return TRUE;
}
