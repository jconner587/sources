#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/fl_ask.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Widget.H>
#include <FL/fl_draw.H>
#include <FL/Fl_Scroll.H>
#include <FL/Fl_Text_Buffer.H>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <algorithm>

const char* DEFAULT_DATA_FILE = "weights.dat"; // Use binary extension

enum WeightUnit {
    UNIT_KG = 0,
    UNIT_LBS = 1
};

std::string current_date() {
    time_t now = time(0);
    tm* ltm = localtime(&now);
    std::ostringstream oss;
    oss << 1900 + ltm->tm_year << "-"
        << std::setfill('0') << std::setw(2) << 1 + ltm->tm_mon << "-"
        << std::setfill('0') << std::setw(2) << ltm->tm_mday;
    return oss.str();
}

double kg_to_lbs(double kg) {
    return kg * 2.2046226218;
}

double lbs_to_kg(double lbs) {
    return lbs / 2.2046226218;
}

struct WeightEntry {
    std::string date;
    double weight; // Always stored in kg

    void save(std::ofstream& ofs) const {
        size_t len = date.size();
        ofs.write(reinterpret_cast<const char*>(&len), sizeof(len));
        ofs.write(date.c_str(), len);
        ofs.write(reinterpret_cast<const char*>(&weight), sizeof(weight));
    }

    bool load(std::ifstream& ifs) {
        size_t len;
        if (!ifs.read(reinterpret_cast<char*>(&len), sizeof(len))) return false;
        date.resize(len);
        if (!ifs.read(&date[0], len)) return false;
        if (!ifs.read(reinterpret_cast<char*>(&weight), sizeof(weight))) return false;
        return true;
    }
};

class WeightGraph : public Fl_Widget {
    const std::vector<WeightEntry>& history;
    const double& target_weight;
    const int& target_unit;
    WeightUnit unit;
public:
    WeightGraph(int X, int Y, int W, int H, const std::vector<WeightEntry>& hist,
                const double& tgt, const int& tgt_unit, WeightUnit u)
        : Fl_Widget(X, Y, W, H, 0), history(hist), target_weight(tgt), target_unit(tgt_unit), unit(u) {}

    void set_unit(WeightUnit u) { unit = u; redraw(); }

    double get_weight(const WeightEntry& entry) const {
        return (unit == UNIT_KG) ? entry.weight : kg_to_lbs(entry.weight);
    }

    double get_target_weight() const {
        if (target_weight <= 0) return -1;
        if (unit == UNIT_KG) {
            return (target_unit == UNIT_KG) ? target_weight : lbs_to_kg(target_weight);
        } else {
            return (target_unit == UNIT_KG) ? kg_to_lbs(target_weight) : target_weight;
        }
    }

    void draw() override {
        fl_push_clip(x(), y(), w(), h());
        fl_color(FL_WHITE); fl_rectf(x(), y(), w(), h());
        fl_color(FL_BLACK); fl_rect(x(), y(), w(), h());
        if (history.size() < 2) {
            fl_color(FL_BLACK);
            fl_draw("Not enough data for graph.", x() + 5, y() + 20);
            fl_pop_clip();
            return;
        }
        // Find min/max
        double minw = get_weight(*std::min_element(history.begin(), history.end(),
            [&](const WeightEntry& a, const WeightEntry& b) { return get_weight(a) < get_weight(b); }));
        double maxw = get_weight(*std::max_element(history.begin(), history.end(),
            [&](const WeightEntry& a, const WeightEntry& b) { return get_weight(a) < get_weight(b); }));

        // Expand min/max to cover target line if needed
        double target_w = get_target_weight();
        if (target_w > 0) {
            if (target_w < minw) minw = target_w;
            if (target_w > maxw) maxw = target_w;
        }
        if (minw == maxw) minw -= 1, maxw += 1;

        int N = (int)history.size();
        int gx = x() + 40, gy = y() + 10, gw = w() - 60, gh = h() - 40;
        // Draw axes
        fl_color(FL_GRAY);
        fl_line(gx, gy + gh, gx + gw, gy + gh); // x-axis
        fl_line(gx, gy, gx, gy + gh); // y-axis

        // Y labels (5 ticks)
        fl_color(FL_BLACK);
        for (int i = 0; i <= 5; ++i) {
            double val = minw + (maxw - minw) * i / 5.0;
            int py = gy + gh - (int)((val - minw) / (maxw - minw) * gh);
            char buf[32];
            snprintf(buf, sizeof(buf), "%.1f", val);
            fl_draw(buf, gx - 35, py + 5);
            fl_color(FL_GRAY); fl_line(gx - 3, py, gx + gw, py);
            fl_color(FL_BLACK);
        }

        // Draw target weight as cyan line if set
        if (target_w > 0) {
            int tpy = gy + gh - (int)((target_w - minw) / (maxw - minw) * gh);
            fl_color(FL_CYAN);
            fl_line_style(FL_DASH, 2);
            fl_line(gx, tpy, gx + gw, tpy);
            fl_line_style(0); // reset
            // Draw label for target
            std::ostringstream oss;
            oss << "Target: " << std::fixed << std::setprecision(1) << target_w
                << (unit == UNIT_KG ? " kg" : " lbs");
            fl_draw(oss.str().c_str(), gx + gw - 100, tpy - 5);
        }

        // Draw points and lines
        fl_color(FL_RED);
        int prevx = 0, prevy = 0;
        for (int i = 0; i < N; ++i) {
            int px = gx + (int)((double)i / (N - 1) * gw);
            double wval = get_weight(history[i]);
            int py = gy + gh - (int)((wval - minw) / (maxw - minw) * gh);
            fl_pie(px - 3, py - 3, 6, 6, 0, 360);
            if (i > 0) fl_line(prevx, prevy, px, py);
            prevx = px; prevy = py;
        }
        fl_color(FL_BLACK);
        std::string ylab = (unit == UNIT_KG) ? "kg" : "lbs";
        fl_draw(("Weight (" + ylab + ")").c_str(), x() + 5, y() + 12);
        fl_draw("Date", gx + gw / 2 - 15, gy + gh + 28);
        fl_pop_clip();
    }
};

class WeightTracker : public Fl_Window {
    Fl_Menu_Bar* menu_bar;
    Fl_Input* weight_input;
    Fl_Choice* unit_choice;
    Fl_Button* add_button;
    Fl_Button* clear_button;
    Fl_Button* save_bin_button;
    Fl_Button* load_bin_button;
    Fl_Input* filename_input;
    Fl_Box* info_box;

    Fl_Input* target_input;
    Fl_Choice* target_unit_choice;
    Fl_Button* set_target_button;
    Fl_Box* target_box;
    double target_weight = 0;
    int target_unit = UNIT_KG;

    std::vector<WeightEntry> history;
    std::string current_file;
    WeightUnit graph_unit = UNIT_KG;

    Fl_Window* graph_window = nullptr;
    WeightGraph* graph_widget = nullptr;

    Fl_Text_Display* history_output;
    Fl_Text_Buffer* history_buffer;

    static void graph_menu_cb(Fl_Widget*, void* userdata) {
        WeightTracker* self = (WeightTracker*)userdata;
        self->show_graph();
    }

    static void add_cb(Fl_Widget* w, void* userdata) {
        WeightTracker* self = (WeightTracker*)userdata;
        self->add_entry();
    }
    static void save_bin_cb(Fl_Widget* w, void* userdata) {
        WeightTracker* self = (WeightTracker*)userdata;
        self->save_to_binary_file();
    }
    static void load_bin_cb(Fl_Widget* w, void* userdata) {
        WeightTracker* self = (WeightTracker*)userdata;
        self->load_from_binary_file();
    }
    static void clear_cb(Fl_Widget* w, void* userdata) {
        WeightTracker* self = (WeightTracker*)userdata;
        self->clear_file();
    }
    static void unit_cb(Fl_Widget* w, void* userdata) {
        WeightTracker* self = (WeightTracker*)userdata;
        self->update_unit();
    }
    static void set_target_cb(Fl_Widget* w, void* userdata) {
        WeightTracker* self = (WeightTracker*)userdata;
        self->set_target();
    }
    static void target_unit_cb(Fl_Widget* w, void* userdata) {
        WeightTracker* self = (WeightTracker*)userdata;
        self->target_unit = self->target_unit_choice->value();
        self->update_target_box();
    }
    static void help_menu_cb(Fl_Widget*, void* userdata) {
        Fl_Window* win = (Fl_Window*)userdata;
        fl_message(
            "Weight Tracker Application\n"
            "Version 2.1.1 (Build 2)\n"
            "\n"
            "Instructions:\n"
            "- Enter your weight and select either kg or lbs, then click 'Add'.\n"
            "- The history shows all entries with a scrollbar for convenience.\n"
            "- The menu bar can be used to show a graph of your weight (with target line in CYAN).\n"
            "- Use 'Save Bin' and 'Load Bin' to save/load securely in binary format. Only this program can read the file.\n"
            "- Targets are supported and saved/loaded as well."
            "\n"
            "Created by Josh Conner (c)2025"
        );
    }

    std::string get_diff_message() {
        if (history.size() < 2) {
            return "Enter at least two weights to show difference.";
        }
        double first = history.front().weight;
        double last = history.back().weight;
        double diff = last - first;
        std::ostringstream oss;
        oss << "Since " << history.front().date << ": ";
        if (diff > 0)
            oss << "You gained " << std::fixed << std::setprecision(2) << diff << " kg ("
                << kg_to_lbs(diff) << " lbs).";
        else if (diff < 0)
            oss << "You lost " << std::fixed << std::setprecision(2) << -diff << " kg ("
                << kg_to_lbs(-diff) << " lbs).";
        else
            oss << "No weight change.";
        return oss.str();
    }

    void update_history_display() {
        std::ostringstream oss;
        oss << "Date\t\tWeight (kg)\tWeight (lbs)\n";
        oss << "------------------------------------------\n";
        for (const auto& entry : history) {
            oss << entry.date << "\t" << entry.weight << "\t\t"
                << std::fixed << std::setprecision(2) << kg_to_lbs(entry.weight) << "\n";
        }
        oss << "------------------------------------------\n";
        oss << get_diff_message() << "\n";
        history_buffer->text(oss.str().c_str());
        update_target_box();
        if (graph_widget) graph_widget->redraw();

        // Scroll to bottom (using Fl_Text_Display's scroll)
        int last_line = history_buffer->count_lines(0, history_buffer->length());
        history_output->scroll(last_line, 0);
    }

    void update_target_box() {
        if (target_weight > 0) {
            std::ostringstream oss;
            if (target_unit == UNIT_KG) {
                oss << "Target: " << std::fixed << std::setprecision(2)
                    << target_weight << " kg (" << kg_to_lbs(target_weight) << " lbs)";
            } else {
                oss << "Target: " << std::fixed << std::setprecision(2)
                    << kg_to_lbs(target_weight) << " lbs (" << target_weight << " kg)";
            }
            target_box->label(oss.str().c_str());
        } else {
            target_box->label("Target: Not set");
        }
    }

    void add_entry() {
        std::string weight_str = weight_input->value();
        double weight_val = 0;
        try {
            weight_val = std::stod(weight_str);
        } catch (...) {
            info_box->label("Invalid weight value.");
            return;
        }
        if (weight_val <= 0) {
            info_box->label("Weight must be positive.");
            return;
        }
        int selected_unit = unit_choice->value();
        if (selected_unit == UNIT_LBS) {
            weight_val = lbs_to_kg(weight_val);
        }
        WeightEntry entry{current_date(), weight_val};
        history.push_back(entry);
        update_history_display();
        info_box->label("Entry added.");
        weight_input->value("");

        if (target_weight > 0) {
            bool reached = false;
            if (history.size() > 1) {
                double prev = history[history.size()-2].weight;
                if (prev > target_weight && weight_val <= target_weight) reached = true;
                if (prev < target_weight && weight_val >= target_weight) reached = true;
            } else {
                if (weight_val == target_weight) reached = true;
            }
            if (reached) {
                fl_message("Congratulations! You reached your target weight of %.2f kg (%.2f lbs).",
                           target_weight, kg_to_lbs(target_weight));
                int answer = fl_choice(
                    "Would you like to set a new target weight?",
                    "No", "Yes", NULL
                );
                if (answer == 1) {
                    target_input->value("");
                    target_input->take_focus();
                } else {
                    target_weight = 0;
                    update_target_box();
                }
            }
        }
    }

    void update_unit() {
        graph_unit = (WeightUnit)unit_choice->value();
        update_history_display();
        if (graph_widget) graph_widget->set_unit(graph_unit);
    }

    // --- BINARY SAVE/LOAD ONLY ---
    void save_to_binary_file() {
        std::string file = filename_input->value();
        if (file.empty()) file = DEFAULT_DATA_FILE;
        std::ofstream ofs(file, std::ios::binary);
        if (!ofs) {
            info_box->label("Error saving binary file.");
            return;
        }
        size_t count = history.size();
        ofs.write(reinterpret_cast<const char*>(&count), sizeof(count));
        for (const auto& entry : history) {
            entry.save(ofs);
        }
        ofs.write(reinterpret_cast<const char*>(&target_weight), sizeof(target_weight));
        ofs.write(reinterpret_cast<const char*>(&target_unit), sizeof(target_unit));
        info_box->label(("Binary data saved to " + file).c_str());
    }

    void load_from_binary_file() {
        std::string file = filename_input->value();
        if (file.empty()) file = DEFAULT_DATA_FILE;
        std::ifstream ifs(file, std::ios::binary);
        if (!ifs) {
            info_box->label("No binary file to load.");
            return;
        }
        size_t count = 0;
        if (!ifs.read(reinterpret_cast<char*>(&count), sizeof(count))) {
            info_box->label("Corrupt binary file.");
            return;
        }
        history.clear();
        for (size_t i = 0; i < count; ++i) {
            WeightEntry entry;
            if (!entry.load(ifs)) {
                info_box->label("Corrupt binary file.");
                return;
            }
            history.push_back(entry);
        }
        if (!ifs.read(reinterpret_cast<char*>(&target_weight), sizeof(target_weight)) ||
            !ifs.read(reinterpret_cast<char*>(&target_unit), sizeof(target_unit))) {
            target_weight = 0;
            target_unit = UNIT_KG;
        }
        update_history_display();
        info_box->label(("Binary data loaded from " + file).c_str());
    }

    void clear_file() {
        std::string file = filename_input->value();
        if (file.empty()) file = DEFAULT_DATA_FILE;
        std::ofstream ofs(file, std::ios::trunc | std::ios::binary);
        if (!ofs) {
            info_box->label("Error clearing file.");
            return;
        }
        ofs.close();
        history.clear();
        target_weight = 0;
        target_unit = UNIT_KG;
        if (target_unit_choice) target_unit_choice->value(target_unit);
        update_history_display();
        info_box->label("Data cleared.");
    }

    void set_target() {
        std::string target_str = target_input->value();
        if (target_str.empty()) {
            info_box->label("Please enter a target weight.");
            return;
        }
        double val = 0;
        try {
            val = std::stod(target_str);
        } catch (...) {
            info_box->label("Invalid target weight.");
            return;
        }
        if (val <= 0) {
            info_box->label("Target must be positive.");
            return;
        }
        int selected_unit = target_unit_choice->value();
        target_unit = selected_unit;
        if (selected_unit == UNIT_LBS) val = lbs_to_kg(val);
        target_weight = val;
        info_box->label("Target set.");
        update_target_box();
    }

    void show_graph() {
        if (graph_window) {
            graph_window->show();
            graph_window->take_focus();
            return;
        }
        int gw = 500, gh = 320;
        graph_window = new Fl_Window(gw, gh, "Weight History Graph");
        graph_widget = new WeightGraph(20, 20, gw - 40, gh - 40, history, target_weight, target_unit, graph_unit);
        graph_window->end();
        graph_window->set_non_modal();
        graph_window->show();
    }

public:
    WeightTracker(int w, int h, const char* title = 0)
        : Fl_Window(w, h, title)
    {
        begin();
        menu_bar = new Fl_Menu_Bar(0, 0, w, 25);
        menu_bar->add("&Help/About", 0, help_menu_cb, this);
        menu_bar->add("&View/Weight Graph", 0, graph_menu_cb, this);

        int top_offset = 25;
        weight_input = new Fl_Input(120, 20 + top_offset, 100, 30, "Weight:");
        unit_choice = new Fl_Choice(230, 20 + top_offset, 70, 30);
        unit_choice->add("kg|lbs");
        unit_choice->value(0);
        unit_choice->callback(unit_cb, this);

        add_button = new Fl_Button(310, 20 + top_offset, 60, 30, "Add");
        add_button->callback(add_cb, this);

        clear_button = new Fl_Button(380, 20 + top_offset, 60, 30, "Clear");
        clear_button->callback(clear_cb, this);

        filename_input = new Fl_Input(120, 60 + top_offset, 180, 30, "File:");
        filename_input->value(DEFAULT_DATA_FILE);

        save_bin_button = new Fl_Button(310, 60 + top_offset, 90, 30, "Save Bin");
        save_bin_button->callback(save_bin_cb, this);

        load_bin_button = new Fl_Button(410, 60 + top_offset, 90, 30, "Load Bin");
        load_bin_button->callback(load_bin_cb, this);

        info_box = new Fl_Box(FL_NO_BOX, 120, 95 + top_offset, 250, 30, "");
        info_box->labelsize(12);
        info_box->labelfont(FL_ITALIC);

        target_input = new Fl_Input(120, 100 + top_offset, 70, 25, "Target:");
        target_unit_choice = new Fl_Choice(200, 100 + top_offset, 50, 25);
        target_unit_choice->add("kg|lbs");
        target_unit_choice->value(0);
        target_unit_choice->callback(target_unit_cb, this);

        set_target_button = new Fl_Button(260, 100 + top_offset, 70, 25, "Set Target");
        set_target_button->callback(set_target_cb, this);
        target_box = new Fl_Box(FL_NO_BOX, 340, 100 + top_offset, 170, 25, "Target: Not set");
        target_box->labelsize(12);
        target_box->labelfont(FL_ITALIC);

        // History output with scroll and buffer
        history_output = new Fl_Text_Display(20, 140 + top_offset, 420, 150, "");
        history_output->textfont(FL_COURIER);
        history_output->textsize(12);
        history_buffer = new Fl_Text_Buffer();
        history_output->buffer(history_buffer);

        end();
        resizable(this);
        update_history_display();
    }

    ~WeightTracker() {
        delete history_buffer;
    }
};

int main(int argc, char** argv) {
    Fl::scheme("gtk+");
    WeightTracker wt(520, 340, "Weight Tracker 2.1.1");
    wt.show(argc, argv);
    return Fl::run();
}