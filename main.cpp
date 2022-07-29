/*
 * Attribution-ShareAlike 4.0 International (CC BY-SA 4.0) 
 * You are free to:
 * Share — copy and redistribute the material in any medium or format
 * Adapt — remix, transform, and build upon the material
 * for any purpose, even commercially. 
 */

/* 
 * File:   main.cpp
 * Author: MKjellqvist@github
 *
 * Created on February 14, 2021, 2:34 PM
 */

#include <iostream>
#include <functional>
#include <cassert>
#include <gtkmm-3.0/gtkmm/container.h>

#include <gtkmm-3.0/gtkmm.h>
#include <gtkmm-3.0/gtkmm/filechooser.h>
#include <atomic>
#include <thread>

#undef DEBUG_EOM

/**
 * Please don't modify after initialization.
 */
std::vector<std::string> ALLOWED_EXTENSIONS;

void find_allowed_image_formats() {
    auto formats = Gdk::Pixbuf::get_formats();
    for (const auto &format: formats) {
        auto extensions = format.get_extensions();
        for (const auto &ext: extensions) {
            ALLOWED_EXTENSIONS.push_back(ext);
        }
    }
}

struct AppWidgets {
    AppWidgets() = default;

    Gtk::Image *image = nullptr;
    Glib::RefPtr<Gtk::Builder> builder;
    Glib::RefPtr<Gdk::Pixbuf> pixbuf; // should have adda a pixbufloader
    Gtk::ApplicationWindow *main_window = nullptr;
    Gtk::ScrolledWindow *scrolled_window = nullptr;
    Gtk::Viewport *viewport = nullptr;
    Gtk::Overlay *overlay = nullptr;
    Gtk::PopoverMenu *menu = nullptr;
    Gtk::MenuButton *headerButton = nullptr;
    Gtk::HeaderBar *headerBar = nullptr;
    Gtk::Label *overlay_label = nullptr;
    Gtk::Dialog *save_unsaved_dialog = nullptr;
    Gtk::Label *unsaved_text_label = nullptr;
} app_widgets;


// forward for struct member
void next_image();

void show_image(bool update_label);

std::string get_directory_from_file(const std::string &filename) {
    auto file = Gio::File::create_for_path(filename);
    auto directory = file->get_parent()->get_parse_name();
    return directory;
}


struct AppState {

    bool add_file(const std::string &filename) {
        auto file = Gio::File::create_for_path(filename);
        auto name = file->get_basename();
        auto dot = name.find_last_of('.');
        if (dot == std::string::npos) {
#ifdef DEBUG_EOM
            std::cerr << "rejected " << filename << "\n";
#endif
            return false;
        }
        auto ext = name.substr(dot + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), tolower);
        if (std::count(ALLOWED_EXTENSIONS.begin(), ALLOWED_EXTENSIONS.end(), ext)) {
            filelist.push_back(filename);
            return true;
        }
#ifdef DEBUG_EOM
        std::cerr << "rejected " << filename << "\n";
#endif
        return false;
    }

    /**
     * Add files with add_file()
     */
    std::vector<std::string> filelist;

    std::string last_directory;

    std::vector<Glib::RefPtr<Gdk::Pixbuf>> preloaded;

    std::function<void()> zoomAdjustment;

    bool label_showing = true;
    bool fit_to_window = true;
    bool fullscreen = false;
    bool is_moving = false;
    double move_start_x = 0;
    double move_start_y = 0;
    double zoom = 1.0;
    double old_zoom = 1.0;

    std::map<std::string, Gdk::PixbufRotation> rotations;
    size_t image_index = -1;
    Glib::Dispatcher drawScaledDispatcher;
    Glib::Dispatcher drawDispatcher;
    struct ImageDraw {
        int width = 0;
        int height = 0;
    } image_draw_params;
    /**
     * Unintrusive difference type alias
     */
    using st = decltype(filelist)::difference_type;

    [[nodiscard]]
    int slideshow_interval_millis() const {
        return static_cast<int>(slideshow_interval);
    }

    struct interval {
        static constexpr std::array intervals = {10.0, 20.0, 50.0, 100.0, 250.0, 500.0, 750.0, 1000.0, 1400.0, 2000.0,
                                                 5000.0, 10000.0, 20000.0, 60000.0, 300000.0, 900000.0};
        size_t index = 9; // starting at 2 s inteval.
#pragma clang diagnostic push
#pragma ide diagnostic ignored "google-explicit-constructor"

        [[nodiscard]]
        operator double() const {
            return intervals[index];
        }

#pragma clang diagnostic pop

        void faster() {
            if (index > 0) {
                index--;
            }
        }

        void slower() {
            if (index < intervals.size() - 1) {
                index++;
            }
        }
    } slideshow_interval;

    sigc::connection slideshow_connection;

    void toggle_slideshow() {
        if (!slideshow_connection.empty()) {
            slideshow_connection.disconnect();
            return;
        }
        auto slideshow = [this]() -> bool {
            next_image();
            return image_index != filelist.size() - 1;
        };
        slideshow_connection = Glib::signal_timeout().connect(slideshow, slideshow_interval_millis());
    }

    void next() {
        current_directory_index++;
        image_index = (image_index + 1) % filelist.size();
        if (current_directory_index >= current_directory_count) {
            update_current_directory();
            current_directory_index = 0;
        }
    }

    void previous() {
        image_index = image_index ? image_index - 1 : filelist.size() - 1;

        current_directory_index--;
        if (current_directory_index < 0) {
            update_current_directory();
            current_directory_index = current_directory_count - 1;
        }


    }

    void update_current_directory() {
        update_current_directory(get_directory_from_file(filelist[image_index]));
    }

    void update_current_directory(const std::string &new_directory) {
        current_directory = new_directory;

        auto same_directory = [this](const std::string &f) {
            auto file = Gio::File::create_for_path(f);
            return get_directory_from_file(file->get_path()) == this->current_directory;
        };
#ifdef DEBUG_EOM
        std::cerr << new_directory << "\n";
#endif
        auto file_it = filelist.begin() + st(image_index);
        auto r_file_it = std::make_reverse_iterator(file_it);
        // no double-counts of itself. apparently make_reverse magically fixes that.
        auto after = std::count_if(file_it, filelist.end(), same_directory);
        auto before = std::count_if(r_file_it, filelist.rend(), same_directory);

        current_directory_count = after + before;
#ifdef DEBUG_EOM
        std::cerr << "before: " << before << "\n";
        std::cerr << "after: " << after << "\n";
#endif
        auto cur = std::find(filelist.begin(), filelist.end(), current());
#ifdef DEBUG_EOM
        std::cerr << "cur: " << *cur << "\n";
#endif

    }

    std::string get_directory_name() const {
        auto file = Gio::File::create_for_path(current_directory);
        auto directory_name = file->get_basename();
        return directory_name;
    }

    std::string current() {
        assert(image_index < filelist.size());
        assert(image_index >= 0);
        return filelist[image_index];
    }

    std::string current_directory;
    long current_directory_index = -1;
    long current_directory_count = 0;

    std::string reset() {
        image_index = 0;
        current_directory_index = 0;
        update_current_directory();
        return current();
    }

    std::string in_current_directory() const {
        return get_directory_name() + " " + std::to_string(current_directory_index + 1) + "/" +
               std::to_string(current_directory_count);
    }

    std::string current_name() {
        if (filelist.empty()) {
            return "";
        }
        auto file = Gio::File::create_for_path(current());
        return file->get_basename();
    }

    std::string percentage_done() const {
        if (filelist.empty()) {
            return "";
        }
        // 100% at one image, 50% + 100% at two.
        std::stringstream ss;
        ss.precision(2);

        double pct = 100 * double(image_index + 1) * 1.0 / double(filelist.size());
        ss << std::fixed << pct;
        return ss.str() + "%";
    }

    std::string zoom_text() const {
        std::string zoom_text;
        if (zoom != 1.0) {
            zoom_text = std::to_string(unsigned(zoom * 100));
            zoom_text = " " + zoom_text + "%";
        }
        return zoom_text;
    }

    std::string label() {
        auto text = current_name();
        text += zoom_text() + ", " + in_current_directory() + " " + percentage_done();
        return text;
    }

/**
 * Call this after zoom changes, pass the old zoom as value.
 * Change h_adjust with
 *
 * TODO: Cleanup
 *
 * @param y mouse clicked here
 * @param old_zoom
 * @return the new h_adjust value
 */
    double get_vadjust_at_y(double y) const {
        auto v_adjust = app_widgets.scrolled_window->get_vadjustment();
        auto adjust_value = v_adjust->get_value();
        auto zoom_change = (zoom - old_zoom);

        auto child_height = app_widgets.pixbuf->get_height() * (1 + zoom_change);
        auto view_height = app_widgets.scrolled_window->get_height();
        auto c_v_ratio = double(child_height) / double(view_height);
        if (c_v_ratio <= 1.0) {
            return 0;
        }

        auto hidden_pixels_top = adjust_value;
        auto total_y = y + hidden_pixels_top;
        auto expansion_at_total_y = zoom_change * total_y / old_zoom;

        return adjust_value + expansion_at_total_y;
        // om x = 0 hadjust = 0 ska 0 (min) returneras.
        // om x = viewport.size_x och h_adjust = viewport.size_x - image_width * zoom ska hadjust_max returneras
        // borde ju fan vara linjärt mellan 0 och max
    }

/**
 * Call this after zoom changes, pass the old zoom as value.
 * Change h_adjust with
 *
 * TODO: Cleanup
 *
 * @param x mouse clicked here
 * @param old_zoom
 * @return the new h_adjust value
 */
    double get_hadjust_at_x(double x) const {
        auto h_adjust = app_widgets.scrolled_window->get_hadjustment();
        auto adjust_value = h_adjust->get_value();
        auto zoom_change = (zoom - old_zoom);
        auto child_width = app_widgets.pixbuf->get_width() * (1 + zoom_change);
        auto view_width = app_widgets.scrolled_window->get_width();
        auto c_v_ratio = double(child_width) / double(view_width);
        if (c_v_ratio <= 1.0) {
            return 0;
        }

        auto hidden_pixels_left = adjust_value;
        auto total_x = x + hidden_pixels_left;
        auto expansion_at_total_x = zoom_change * total_x / old_zoom;

        return adjust_value + expansion_at_total_x;
    }

    void calculateZoomAdjustment() {
        if (zoom == old_zoom) {
            zoomAdjustment = nullptr;
            return;
        }
        auto h_adjust = app_widgets.scrolled_window->get_hadjustment();
        auto v_adjust = app_widgets.scrolled_window->get_vadjustment();

        auto new_v_adjust_value = get_vadjust_at_y(last_mouse_y);
        auto new_h_adjust_value = get_hadjust_at_x(last_mouse_x);
        auto zoomAdjust = [new_h_adjust_value, new_v_adjust_value, h_adjust, v_adjust]() {
            h_adjust->set_value(new_h_adjust_value);
            h_adjust->value_changed();
            v_adjust->set_value(new_v_adjust_value);
            v_adjust->value_changed();
        };
        zoomAdjustment = zoomAdjust;
    }


    void zoom_out(double x, double y) {
        last_mouse_x = x;
        last_mouse_y = y;

        old_zoom = zoom;
        zoom /= ZOOM_FACTOR;
        if (zoom < 0.01) zoom = 0.01;// lets be reasonable.
        calculateZoomAdjustment();

        show_image(true);
    }

    void zoom_in(double x, double y) {
        last_mouse_x = x;
        last_mouse_y = y;

        old_zoom = zoom;
        zoom *= ZOOM_FACTOR;
        if (zoom > 100.0) zoom = 100.0;// lets be reasonable.

        calculateZoomAdjustment();

        show_image(true);
    }

    double ZOOM_FACTOR = 1.1001; // 1001 improves FP errors
    double last_mouse_x = 0;
    double last_mouse_y = 0;
} app_state;


void toggle_show_label() {
    if (app_state.label_showing) {
        app_widgets.overlay_label->hide();
    } else {
        app_widgets.overlay_label->show();
    }
    app_state.label_showing = !app_state.label_showing;
}

std::atomic<bool> drawing = false;


void on_image_noscale_notify() {
    app_widgets.image->set(app_widgets.pixbuf);
}

void on_image_notify() {
    app_widgets.image->set(
            app_widgets.pixbuf->scale_simple(app_state.image_draw_params.width, app_state.image_draw_params.height,
                                             Gdk::INTERP_BILINEAR));
    if (app_state.zoomAdjustment) {
        app_state.zoomAdjustment();
        app_state.zoomAdjustment = nullptr;
    }
}

void draw_current() {
    auto noscale = true;
    try {
        app_widgets.pixbuf = Gdk::Pixbuf::create_from_file(app_state.current());

        auto rotation = app_state.rotations[app_state.current()];
        if (rotation != Gdk::PixbufRotation::PIXBUF_ROTATE_NONE) {
            app_widgets.pixbuf = app_widgets.pixbuf->rotate_simple(rotation);
        }
        if (app_state.zoom != 1.0 && !app_state.fit_to_window) {
            app_state.image_draw_params.width = int(app_state.zoom * app_widgets.pixbuf->get_width());
            app_state.image_draw_params.height = int(app_state.zoom * app_widgets.pixbuf->get_height());
            noscale = false;
            app_state.fit_to_window = false;
        }
        if (app_state.fit_to_window) {
            auto win_client = app_widgets.scrolled_window->get_clip();
            auto win_ratio = win_client.get_width() * 1.0 / win_client.get_height();
            auto img_ratio = app_widgets.pixbuf->get_width() * 1.0 / app_widgets.pixbuf->get_height();

            if (win_ratio > img_ratio) { // win wider than image
                app_state.image_draw_params.width = int(app_state.zoom * win_client.get_height() * img_ratio);
                app_state.image_draw_params.height = int(app_state.zoom * win_client.get_height());
                noscale = false;
            } else {
                app_state.image_draw_params.width = int(app_state.zoom * win_client.get_width());
                app_state.image_draw_params.height = int(app_state.zoom * win_client.get_width() / img_ratio);
                noscale = false;
            }
        } else { // no scale

        }
    } catch (Glib::Error &error) {
        std::cerr << error.what() << "\n";
    }

    if (noscale) {
        app_state.drawDispatcher.emit();
    } else {
        app_state.drawScaledDispatcher.emit();
    }
    drawing = false;
}

void show_image(bool update_label = false) {
    if (app_state.filelist.empty()) {
        return;
    }
    if (update_label)
        app_widgets.overlay_label->set_text(app_state.label());
    if (drawing) {
        return;
    }

    drawing = true;
    std::thread drawer(draw_current);
    drawer.detach();
}

template<typename Func>
auto action_activate(Func f) {
    return [f](const Glib::VariantBase &huh) {
        f();
    };
}

void dont_save_rotated_files() {
    app_widgets.save_unsaved_dialog->hide();
}

void set_changed_text_label(long changed) {
    std::string text = "There are ";
    text += std::to_string(changed) + " unsaved images with changes. Do you want to save the changes?";
    app_widgets.unsaved_text_label->set_text(text);
    app_widgets.save_unsaved_dialog->queue_draw();
}

void save_rotated_files() {
    using pair = decltype(*app_state.rotations.begin());
    auto rotation_not_none = [](pair p) {
        return p.second != Gdk::PIXBUF_ROTATE_NONE;
    };
    auto rotated = count_if(app_state.rotations.begin(), app_state.rotations.end(), rotation_not_none);

    for (auto p: app_state.rotations) {
        if (rotation_not_none(p)) {
            auto pixbuf = Gdk::Pixbuf::create_from_file(p.first);
            pixbuf = pixbuf->rotate_simple(p.second);
            pixbuf->save(p.first, "jpeg");
            rotated--;
            app_state.rotations[p.first] = Gdk::PIXBUF_ROTATE_NONE;
            set_changed_text_label(rotated);
        }
    }

}

void check_save_on_exit() {
    using pair = decltype(*app_state.rotations.begin());
    auto rotation_not_none = [](pair p) {
        return p.second != Gdk::PIXBUF_ROTATE_NONE;
    };
    auto rotated = count_if(app_state.rotations.begin(), app_state.rotations.end(), rotation_not_none);

    if (rotated) {
        set_changed_text_label(rotated);
        //app_widgets.save_unsaved_dialog->show();
        if (app_widgets.save_unsaved_dialog->run() == Gtk::RESPONSE_YES) {
            save_rotated_files();
        }
        app_widgets.save_unsaved_dialog->hide();
    }
}

/**
 * Quits is the app is in windowed mode
 */
void leave_fullscreen_or_leave_app() {
    if (app_state.fullscreen) {
        app_widgets.main_window->unfullscreen();
        app_state.fullscreen = false;
    } else {
        check_save_on_exit();
        app_widgets.main_window->close();
    }
}

void fit() {
    app_state.fit_to_window = true;
    show_image();
}

void norescale() {
    app_state.fit_to_window = false;
    app_state.zoom = 1.0;
    show_image();
}

void toggle_fullscreen() {
    if (app_state.fullscreen) {
        app_widgets.main_window->unfullscreen();
        app_state.fullscreen = false;
    } else {
        app_widgets.main_window->fullscreen();
        app_state.fullscreen = true;
    }
    app_widgets.scrolled_window->set_max_content_height(app_widgets.overlay->get_height());
    app_widgets.scrolled_window->set_max_content_width(app_widgets.overlay->get_width());
    app_widgets.scrolled_window->set_policy(Gtk::PolicyType::POLICY_AUTOMATIC, Gtk::PolicyType::POLICY_AUTOMATIC);
    app_widgets.scrolled_window->set_border_width(40);

}

void next_image() {
    if (app_state.filelist.empty())
        return;
    app_state.next();
    show_image(true);
}

void prev_image() {
    if (app_state.filelist.empty())
        return;
    app_state.previous();
    show_image(true);
}

void rotate_left() {
    auto &rotation = app_state.rotations[app_state.current()];
    switch (rotation) {
        case Gdk::PixbufRotation::PIXBUF_ROTATE_NONE:
            rotation = Gdk::PixbufRotation::PIXBUF_ROTATE_COUNTERCLOCKWISE;
            break;
        case Gdk::PixbufRotation::PIXBUF_ROTATE_COUNTERCLOCKWISE:
            rotation = Gdk::PixbufRotation::PIXBUF_ROTATE_UPSIDEDOWN;
            break;
        case Gdk::PixbufRotation::PIXBUF_ROTATE_UPSIDEDOWN:
            rotation = Gdk::PixbufRotation::PIXBUF_ROTATE_CLOCKWISE;
            break;
        default:
            rotation = Gdk::PixbufRotation::PIXBUF_ROTATE_NONE;
    }

    show_image();
}

void rotate_right() {
    auto &rotation = app_state.rotations[app_state.current()];

    switch (rotation) {
        case Gdk::PixbufRotation::PIXBUF_ROTATE_NONE:
            rotation = Gdk::PixbufRotation::PIXBUF_ROTATE_CLOCKWISE;
            break;
        case Gdk::PixbufRotation::PIXBUF_ROTATE_CLOCKWISE:
            rotation = Gdk::PixbufRotation::PIXBUF_ROTATE_UPSIDEDOWN;
            break;
        case Gdk::PixbufRotation::PIXBUF_ROTATE_UPSIDEDOWN:
            rotation = Gdk::PixbufRotation::PIXBUF_ROTATE_COUNTERCLOCKWISE;
            break;
        default:
            rotation = Gdk::PixbufRotation::PIXBUF_ROTATE_NONE;
    }
    show_image();
}

void next_directory() {
    auto ctx = app_state.current_directory;
    do {
        app_state.next();
        if (app_state.image_index == 0) {
            break;
        }
        auto cur = app_state.filelist[app_state.image_index];
        ctx = get_directory_from_file(cur);

    } while (app_state.current_directory == ctx);
    show_image(true);
}

void prev_directory() {
    auto ctx = app_state.current_directory;
    while (ctx == app_state.current_directory) {
        if (app_state.image_index == 0) {
            app_state.image_index = app_state.filelist.size();
        }
        app_state.image_index--;

        auto cur = app_state.filelist[app_state.image_index];
        ctx = get_directory_from_file(cur);
    }

    bool underflow = false;
    while (ctx == app_state.current_directory) {
        if (app_state.image_index == 0) {
            underflow = true;
            break;
        }
        app_state.image_index--;
    }
    if (!underflow)
        app_state.image_index++;

    show_image(true);
}

void toggle_slideshow() {
    app_state.toggle_slideshow();
}

void show_open_dialog() {
    Gtk::FileChooserDialog fcd(*app_widgets.main_window, "Select folder",
                               Gtk::FileChooserAction::FILE_CHOOSER_ACTION_OPEN,
                               Gtk::DialogFlags::DIALOG_MODAL | Gtk::DialogFlags::DIALOG_USE_HEADER_BAR);

    fcd.add_button(Gtk::Stock::CANCEL, Gtk::RESPONSE_CANCEL);
    fcd.add_button(Gtk::Stock::OPEN, Gtk::RESPONSE_OK);
    auto res = fcd.run();
    if (res == Gtk::RESPONSE_OK) {
        app_state.image_index = 0;
        auto filename = fcd.get_filename();
        if (app_state.add_file(filename)) {
            show_image(true);
        }
    }
}

#pragma clang diagnostic push
#pragma ide diagnostic ignored "misc-no-recursion"

void add_files_in_dir(const std::string &pathname) {
    auto dir = Gio::File::create_for_path(pathname);
    auto files = dir->enumerate_children();

    for (auto file = files->next_file(); file; file = files->next_file()) {
        if (file->get_file_type() == Gio::FileType::FILE_TYPE_REGULAR) {
            app_state.add_file(pathname + "/" + file->get_name());
        }
        if (file->get_file_type() == Gio::FileType::FILE_TYPE_DIRECTORY) {
            add_files_in_dir(pathname + "/" + file->get_name());
        }
    }

}

#pragma clang diagnostic pop

void show_select_directory() {
    Gtk::FileChooserDialog fcd(*app_widgets.main_window, "Select folder",
                               Gtk::FileChooserAction::FILE_CHOOSER_ACTION_SELECT_FOLDER,
                               Gtk::DialogFlags::DIALOG_MODAL | Gtk::DialogFlags::DIALOG_USE_HEADER_BAR);

    if (!app_state.last_directory.empty()) {
        fcd.set_current_folder(app_state.last_directory);
    }
    fcd.add_button(Gtk::Stock::CANCEL, Gtk::RESPONSE_CANCEL);
    fcd.add_button(Gtk::Stock::OPEN, Gtk::RESPONSE_OK);
    auto res = fcd.run();
    if (res == Gtk::RESPONSE_OK) {
        auto pathname = fcd.get_filename();
        app_state.last_directory = pathname;
#ifdef DEBUG_EOM
        std::cerr << "Added " << app_state.filelist.size() << " images.";
        std::cerr << pathname << "\n";
#endif
        app_state.filelist.clear();
        add_files_in_dir(pathname);

        app_state.reset();
        show_image(true);
    }
}

void faster_slideshow() {
    app_state.slideshow_interval.faster();
    auto label_text = std::to_string(app_state.slideshow_interval_millis()) + " ms";
    // quick_toggle to reconnect with new interval, after interval change
    app_state.toggle_slideshow();
    app_state.toggle_slideshow();

    app_widgets.overlay_label->set_text(label_text);
}

void slower_slideshow() {
    app_state.slideshow_interval.slower();
    auto label_text = std::to_string(app_state.slideshow_interval_millis()) + " ms";
    // quick_toggle to reconnect with new interval, after interval change
    app_state.toggle_slideshow();
    app_state.toggle_slideshow();

    app_widgets.overlay_label->set_text(label_text);
}


template<typename Func>
void add_win_action_and_connection(const std::string &short_action_name, Func func) {
    auto action = app_widgets.main_window->add_action(short_action_name);
    action->signal_activate().connect(action_activate(func), true);
    action->set_enabled(true);
}

void setup_actions_and_connections() {
    add_win_action_and_connection("open", show_open_dialog);
    add_win_action_and_connection("fullscreen", toggle_fullscreen);
    add_win_action_and_connection("open-directory", show_select_directory);
    add_win_action_and_connection("show-legend", next_image);
    add_win_action_and_connection("zoom", norescale);
    add_win_action_and_connection("next", next_image);
    add_win_action_and_connection("previous", prev_image);
    add_win_action_and_connection("leave-fullscreen", leave_fullscreen_or_leave_app);
    add_win_action_and_connection("fit", fit);
    add_win_action_and_connection("norescale", norescale);
    add_win_action_and_connection("slideshow", toggle_slideshow);
    add_win_action_and_connection("toggle-label", toggle_show_label);
    add_win_action_and_connection("slower-slideshow", slower_slideshow);
    add_win_action_and_connection("faster-slideshow", faster_slideshow);
    add_win_action_and_connection("next-directory", next_directory);
    add_win_action_and_connection("prev-directory", prev_directory);
    add_win_action_and_connection("rotate-left", rotate_left);
    add_win_action_and_connection("rotate-right", rotate_right);
    add_win_action_and_connection("save", check_save_on_exit);
    add_win_action_and_connection("hide", dont_save_rotated_files);

}

void update_adjustment(double x, double y) {
    auto dx = app_state.move_start_x - x;
    auto dy = app_state.move_start_y - y;

    auto hscroll = app_widgets.scrolled_window->get_hadjustment();
    auto hcurrent = hscroll->get_value();
    hscroll->set_value(hcurrent + dx);
    app_widgets.scrolled_window->set_hadjustment(hscroll);
    auto vscroll = app_widgets.scrolled_window->get_vadjustment();
    auto vcurrent = vscroll->get_value();
    vscroll->set_value(vcurrent + dy);
    app_widgets.scrolled_window->set_vadjustment(vscroll);

    app_state.move_start_x = x;
    app_state.move_start_y = y;
}

#pragma clang diagnostic push
#pragma ide diagnostic ignored "ConstantFunctionResult"

/*
 * 
 */
int main(int argc, char **argv) {
    auto app = Gtk::Application::create(argc, argv, "se.miun.markje", Gio::ApplicationFlags::APPLICATION_FLAGS_NONE);

    app_state.drawDispatcher.connect(&on_image_noscale_notify);
    app_state.drawScaledDispatcher.connect(&on_image_notify);
    auto recent_manager = Gtk::RecentManager::get_default();
    auto wd = Glib::get_current_dir();
    recent_manager->add_item(wd);

    find_allowed_image_formats();

    app_widgets.builder = Gtk::Builder::create_from_resource("/ui/eom.glade");
    app_widgets.builder->set_application(app);

    app_widgets.builder->get_widget("main_window", app_widgets.main_window);
    app_widgets.builder->get_widget("main_image", app_widgets.image);
    app_widgets.builder->get_widget("scrolled_window", app_widgets.scrolled_window);
    app_widgets.builder->get_widget("viewport", app_widgets.viewport);
    app_widgets.builder->get_widget("header_menu_button", app_widgets.headerButton);
    app_widgets.builder->get_widget("popover", app_widgets.menu);
    app_widgets.builder->get_widget("overlay", app_widgets.overlay);
    app_widgets.builder->get_widget("header", app_widgets.headerBar);
    app_widgets.builder->get_widget("overlay_label", app_widgets.overlay_label);
    app_widgets.builder->get_widget("save_dialog", app_widgets.save_unsaved_dialog);
    app_widgets.builder->get_widget("unsaved_text_label", app_widgets.unsaved_text_label);

    app_widgets.overlay_label->override_background_color(Gdk::RGBA("rgba(255,255,255,0.6)"));
    app_widgets.overlay_label->override_color(Gdk::RGBA("rgb(0,0,0)"));
    app_widgets.overlay_label->set_opacity(0.6);
    app_widgets.overlay_label->set_text(app_state.label());
    app_widgets.image->override_background_color(Gdk::RGBA("#000"));

    // Setup / fix main_window
    Gtk::Box m_customHeaderBar;
    Gtk::Entry m_entry;
    m_entry.set_hexpand_set(true);
    m_entry.set_hexpand();

    m_customHeaderBar.pack_start(m_entry);
    app_widgets.main_window->set_titlebar(*app_widgets.headerBar);
    auto icon_pixbuf = Gdk::Pixbuf::create_from_resource("/ui/icons/eom.svg");
    app_widgets.main_window->set_icon(icon_pixbuf);

    // Fix icon for save dialog.
    app_widgets.save_unsaved_dialog->set_icon(icon_pixbuf);

    app->add_accelerator("1", "win.norescale");
    app->add_accelerator("section", "win.fit");
    app->add_accelerator("f", "win.fullscreen");
    app->add_accelerator("Left", "win.previous");
    app->add_accelerator("Right", "win.next");
    app->add_accelerator("Escape", "win.leave-fullscreen");
    app->add_accelerator("space", "win.slideshow");
    app->add_accelerator("l", "win.toggle-label");
    app->add_accelerator("Page_Up", "win.faster-slideshow");
    app->add_accelerator("Page_Down", "win.slower-slideshow");
    app->add_accelerator("n", "win.next-directory");
    app->add_accelerator("p", "win.prev-directory");
    app->add_accelerator("z", "win.rotate-left");
    app->add_accelerator("x", "win.rotate-right");

    setup_actions_and_connections();

    try {
        auto menu = Gtk::Builder::create_from_resource("/ui/cog_menu.ui");

        auto object_menu = menu->get_object("app-menu");
        Glib::RefPtr<Gio::Menu> app_menu = Glib::RefPtr<Gio::Menu>::cast_dynamic(object_menu);
        app_widgets.menu->bind_model(app_menu);
    } catch (Gio::ResourceError &re) {
        std::cerr << re.what();
    }
    auto on_key_press = [](GdkEventKey *ek) {
#ifdef EOM_DEBUG
        auto name = gtk_accelerator_name(ek->keyval, GdkModifierType(0));
        std::cerr << name << "\n";
#endif
        return true;
    };

    auto on_show_image = []() {
        show_image();
        return false;
    };

    auto old_x = 0;
    auto old_y = 0;
    auto on_resize = [on_show_image, &old_x, &old_y]() {//GdkEventConfigure * ec)->bool {
        int new_x;
        int new_y;
        app_widgets.main_window->get_size(new_x, new_y);
        if (new_x == old_x && new_y == old_y)
            return;
        Glib::signal_idle().connect(on_show_image);
        old_x = new_x;
        old_y = new_y;
    };

    auto on_mouse_move = [](GdkEventMotion *em) -> bool {
        if (app_state.is_moving) { // mouse == null
            update_adjustment(em->x, em->y);
        }
        return false;
    };
    auto on_mouse_click = [](GdkEventButton *eb) -> bool {
        if (eb->type == GDK_2BUTTON_PRESS) {
            toggle_fullscreen();
            return false;
        }
        if (eb->type != GDK_BUTTON_PRESS)
            return false;
        app_state.is_moving = true;
        app_state.move_start_x = eb->x;
        app_state.move_start_y = eb->y;
        return false;
    };
    auto on_mouse_release = [](GdkEventButton *eb) -> bool {
        if (eb->type != GDK_BUTTON_RELEASE)
            return false;
        app_state.is_moving = false;
        return false;
    };

    auto on_mouse_scroll = [](GdkEventScroll *es) -> bool {
        if (es->direction == GdkScrollDirection::GDK_SCROLL_DOWN) {
            app_state.zoom_out(es->x, es->y);
        }
        if (es->direction == GdkScrollDirection::GDK_SCROLL_UP) {
            app_state.zoom_in(es->x, es->y);
        }
        return false;
    };

    app_widgets.main_window->add_events(
            Gdk::EventMask::BUTTON1_MOTION_MASK | Gdk::EventMask::KEY_PRESS_MASK |
            Gdk::EventMask::BUTTON_PRESS_MASK | Gdk::EventMask::BUTTON_RELEASE_MASK);
    app_widgets.main_window->signal_key_press_event().connect(on_key_press, true);
    app_widgets.main_window->signal_check_resize().connect(on_resize, true);
    app_widgets.main_window->signal_motion_notify_event().connect(on_mouse_move, true);
    app_widgets.main_window->signal_button_press_event().connect(on_mouse_click, true);
    app_widgets.main_window->signal_button_release_event().connect(on_mouse_release, true);
    app_widgets.overlay->add_events(Gdk::EventMask::SCROLL_MASK);
    app_widgets.main_window->signal_scroll_event().connect(on_mouse_scroll, true);

    app_widgets.main_window->show_all();
    return app->run(*app_widgets.main_window);
}

#pragma clang diagnostic pop

