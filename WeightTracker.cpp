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
#include <cmath>

// BMI file for persistence
const char* DEFAULT_BMI_FILE = "bmi_history.dat";
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

// BMI category helper
std::string bmi_category(double bmi) {
    if (bmi < 18.5) return "Underweight";
    if (bmi < 25.0) return "Normal";
    if (bmi < 30.0) return "Overweight";
    return "Obese";
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

struct BMIEntry {
    std::string date;
    double height_m; // always in meters
    double weight_kg;
    double bmi;

    void save(std::ofstream& ofs) const {
        size_t len = date.size();
        ofs.write(reinterpret_cast<const char*>(&len), sizeof(len));
        ofs.write(date.c_str(), len);
        ofs.write(reinterpret_cast<const char*>(&height_m), sizeof(height_m));
        ofs.write(reinterpret_cast<const char*>(&weight_kg), sizeof(weight_kg));
        ofs.write(reinterpret_cast<const char*>(&bmi), sizeof(bmi));
    }
    bool load(std::ifstream& ifs) {
        size_t len;
        if (!ifs.read(reinterpret_cast<char*>(&len), sizeof(len))) return false;
        date.resize(len);
        if (!ifs.read(&date[0], len)) return false;
        if (!ifs.read(reinterpret_cast<char*>(&height_m), sizeof(height_m))) return false;
        if (!ifs.read(reinterpret_cast<char*>(&weight_kg), sizeof(weight_kg))) return false;
        if (!ifs.read(reinterpret_cast<char*>(&bmi), sizeof(bmi))) return false;
        return true;
    }
};

// --------- BMI Dialog Class ----------
#include <FL/Fl_Group.H>

class BMIDialog : public Fl_Window {
    Fl_Input* height_input;
    Fl_Choice* height_unit_choice;
    Fl_Input* weight_input;
    Fl_Box* bmi_result_box;
    Fl_Button* calc_button;
    Fl_Text_Display* bmi_history_display;
    Fl_Text_Buffer* bmi_history_buffer;
    Fl_Button* save_button;
    Fl_Button* load_button;
    std::vector<BMIEntry>& bmi_history;
    std::string bmi_file;

    static void calc_cb(Fl_Widget*, void* userdata) {
        BMIDialog* self = (BMIDialog*)userdata;
        self->calculate_bmi();
    }

    static void save_cb(Fl_Widget*, void* userdata) {
        BMIDialog* self = (BMIDialog*)userdata;
        self->save_history();
    }

    static void load_cb(Fl_Widget*, void* userdata) {
        BMIDialog* self = (BMIDialog*)userdata;
        self->load_history();
    }

    void calculate_bmi() {
        std::string h_str = height_input->value();
        std::string w_str = weight_input->value();
        double height = 0, weight = 0;
        try {
            height = std::stod(h_str);
            weight = std::stod(w_str);
        } catch (...) {
            bmi_result_box->label("Invalid input.");
            return;
        }
        if (height <= 0 || weight <= 0) {
            bmi_result_box->label("Height and weight must be positive.");
            return;
        }
        int h_unit = height_unit_choice->value(); // 0 = cm, 1 = inches
        double height_m = 0.0;
        if (h_unit == 0) height_m = height / 100.0; // convert cm to meters
        else height_m = height * 0.0254; // inches to meters

        double bmi = weight / (height_m * height_m);
        std::ostringstream oss;
        oss << "BMI: " << std::fixed << std::setprecision(2) << bmi
            << " (" << bmi_category(bmi) << ")";
        bmi_result_box->label(oss.str().c_str());

        // Append to in-memory history and buffer
        BMIEntry entry;
        entry.date = current_date();
        entry.height_m = height_m;
        entry.weight_kg = weight;
        entry.bmi = bmi;
        bmi_history.push_back(entry);

        std::ostringstream hline;
        hline << entry.date << "\tBMI: " << std::fixed << std::setprecision(2) << bmi
              << ", " << weight << " kg, "
              << (h_unit == 0 ? height : height * 2.54) << (h_unit == 0 ? " cm" : " in")
              << " [" << bmi_category(bmi) << "]\n";
        bmi_history_buffer->append(hline.str().c_str());
        int lines = bmi_history_buffer->count_lines(0, bmi_history_buffer->length());
        bmi_history_display->scroll(lines, 0);
    }

    void refresh_history_display() {
        bmi_history_buffer->text("");
        for (const auto& entry : bmi_history) {
            double display_height = entry.height_m * 100.0; // cm
            std::ostringstream hline;
            hline << entry.date << "\tBMI: " << std::fixed << std::setprecision(2) << entry.bmi
                  << ", " << entry.weight_kg << " kg, "
                  << display_height << " cm"
                  << " [" << bmi_category(entry.bmi) << "]\n";
            bmi_history_buffer->append(hline.str().c_str());
        }
        int lines = bmi_history_buffer->count_lines(0, bmi_history_buffer->length());
        bmi_history_display->scroll(lines, 0);
    }

    void save_history() {
        std::ofstream ofs(bmi_file, std::ios::binary);
        if (!ofs) {
            fl_alert("Error saving BMI history file.");
            return;
        }
        size_t count = bmi_history.size();
        ofs.write(reinterpret_cast<const char*>(&count), sizeof(count));
        for (const auto& entry : bmi_history) {
            entry.save(ofs);
        }
        fl_message("BMI history saved.");
    }

    void load_history() {
        std::ifstream ifs(bmi_file, std::ios::binary);
        if (!ifs) {
            fl_alert("No BMI history file to load.");
            return;
        }
        size_t count = 0;
        if (!ifs.read(reinterpret_cast<char*>(&count), sizeof(count))) {
            fl_alert("Corrupt BMI history file.");
            return;
        }
        bmi_history.clear();
        for (size_t i = 0; i < count; ++i) {
            BMIEntry entry;
            if (!entry.load(ifs)) {
                fl_alert("Corrupt BMI history file.");
                return;
            }
            bmi_history.push_back(entry);
        }
        refresh_history_display();
        fl_message("BMI history loaded.");
    }

public:
    BMIDialog(double last_weight, std::vector<BMIEntry>& bmi_hist, std::string file = DEFAULT_BMI_FILE)
        : Fl_Window(370, 350, "BMI Calculator"), bmi_history(bmi_hist), bmi_file(file)
    {
        begin();
        new Fl_Box(20, 15, 100, 25, "Height:");
        height_input = new Fl_Input(80, 15, 80, 25);
        height_unit_choice = new Fl_Choice(170, 15, 60, 25);
        height_unit_choice->add("cm|inches");
        height_unit_choice->value(0);

        new Fl_Box(20, 50, 100, 25, "Weight (kg):");
        weight_input = new Fl_Input(110, 50, 80, 25);
        std::ostringstream wstr;
        wstr << std::fixed << std::setprecision(2) << last_weight;
        weight_input->value(wstr.str().c_str());

        calc_button = new Fl_Button(210, 50, 80, 25, "Calculate");
        calc_button->callback(calc_cb, this);

        bmi_result_box = new Fl_Box(FL_NO_BOX, 20, 90, 320, 30, "");
        bmi_result_box->labelsize(14);
        bmi_result_box->labelfont(FL_BOLD);

        save_button = new Fl_Button(80, 320, 90, 25, "Save BMI");
        save_button->callback(save_cb, this);

        load_button = new Fl_Button(190, 320, 90, 25, "Load BMI");
        load_button->callback(load_cb, this);

        new Fl_Box(20, 125, 100, 25, "BMI History:");
        bmi_history_display = new Fl_Text_Display(20, 150, 330, 160, "");
        bmi_history_display->textfont(FL_COURIER);
        bmi_history_display->textsize(12);
        bmi_history_buffer = new Fl_Text_Buffer();
        bmi_history_display->buffer(bmi_history_buffer);

        refresh_history_display();

        end();
        set_non_modal();
    }
    ~BMIDialog() {
        delete bmi_history_buffer;
    }
};

// ---------- UPDATED WEIGHT GRAPH WITH BMI OVERLAY -----------
class WeightGraph : public Fl_Widget {
    const std::vector<WeightEntry>& history;
    const double& target_weight;
    const int& target_unit;
    WeightUnit unit;
    const std::vector<BMIEntry>& bmi_history;
public:
    WeightGraph(int X, int Y, int W, int H, const std::vector<WeightEntry>& hist,
                const double& tgt, const int& tgt_unit, WeightUnit u,
                const std::vector<BMIEntry>& bmi_hist)
        : Fl_Widget(X, Y, W, H, 0), history(hist), target_weight(tgt), target_unit(tgt_unit), unit(u), bmi_history(bmi_hist) {}

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

    // Find min/max for both weight and BMI
    void find_minmax(double& minw, double& maxw, double& minb, double& maxb) const {
        minw = 1e9; maxw = -1e9; minb = 1e9; maxb = -1e9;
        for (auto& e : history) {
            double w = get_weight(e);
            if (w < minw) minw = w;
            if (w > maxw) maxw = w;
        }
        for (auto& e : bmi_history) {
            if (e.bmi < minb) minb = e.bmi;
            if (e.bmi > maxb) maxb = e.bmi;
        }
        double target_w = get_target_weight();
        if (target_w > 0) {
            if (target_w < minw) minw = target_w;
            if (target_w > maxw) maxw = target_w;
        }
        if (minw == maxw) minw -= 1, maxw += 1;
        if (minb == maxb) minb -= 1, maxb += 1;
        if (minb > 25) minb = 15; if (maxb < 35) maxb = 35;
    }

    void draw() override {
        fl_push_clip(x(), y(), w(), h());
        fl_color(FL_WHITE); fl_rectf(x(), y(), w(), h());
        fl_color(FL_BLACK); fl_rect(x(), y(), w(), h());

        // --- Axes and scaling ---
        double minw, maxw, minb, maxb;
        find_minmax(minw, maxw, minb, maxb);

        int gx = x() + 50, gy = y() + 10, gw = w() - 80, gh = h() - 60;
        // Draw axes
        fl_color(FL_GRAY);
        fl_line(gx, gy + gh, gx + gw, gy + gh); // x-axis
        fl_line(gx, gy, gx, gy + gh); // left y-axis (weight)
        fl_line(gx + gw, gy, gx + gw, gy + gh); // right y-axis (BMI)

        // Y labels (5 ticks, left for weight, right for BMI)
        fl_color(FL_BLACK);
        for (int i = 0; i <= 5; ++i) {
            double valw = minw + (maxw - minw) * i / 5.0;
            double valb = minb + (maxb - minb) * i / 5.0;
            int py = gy + gh - (int)((double)i / 5.0 * gh);

            // left: weight
            char buf[32];
            snprintf(buf, sizeof(buf), "%.1f", valw);
            fl_draw(buf, gx - 38, py + 5);

            // right: BMI
            snprintf(buf, sizeof(buf), "%.1f", valb);
            fl_draw(buf, gx + gw + 6, py + 5);

            // grid lines
            if (i != 0 && i != 5) {
                fl_color(FL_LIGHT2);
                fl_line(gx, py, gx + gw, py);
                fl_color(FL_BLACK);
            }
        }

        // Draw target weight as cyan line if set
        double target_w = get_target_weight();
        if (target_w > 0) {
            int tpy = gy + gh - (int)((target_w - minw) / (maxw - minw) * gh);
            fl_color(FL_CYAN); fl_line_style(FL_DASH, 2);
            fl_line(gx, tpy, gx + gw, tpy);
            fl_line_style(0);
            std::ostringstream oss;
            oss << "Target: " << std::fixed << std::setprecision(1) << target_w
                << (unit == UNIT_KG ? " kg" : " lbs");
            fl_draw(oss.str().c_str(), gx + gw - 100, tpy - 6);
        }

        // Draw weight points and lines (red)
        if (history.size() >= 2) {
            fl_color(FL_RED);
            int prevx = 0, prevy = 0;
            int N = (int)history.size();
            for (int i = 0; i < N; ++i) {
                int px = gx + (int)((double)i / (N - 1) * gw);
                double wval = get_weight(history[i]);
                int py = gy + gh - (int)((wval - minw) / (maxw - minw) * gh);
                fl_pie(px - 3, py - 3, 6, 6, 0, 360);
                if (i > 0) fl_line(prevx, prevy, px, py);
                prevx = px; prevy = py;
            }
        }

        // Draw BMI points and lines (blue)
        if (bmi_history.size() >= 2) {
            fl_color(FL_DARK_BLUE);
            int prevx = 0, prevy = 0;
            int N = (int)bmi_history.size();
            for (int i = 0; i < N; ++i) {
                // Find matching date index in weight history for consistent X-positioning, else space evenly
                int px;
                auto it = std::find_if(history.begin(), history.end(),
                    [&](const WeightEntry& e) { return e.date == bmi_history[i].date; });
                if (it != history.end()) {
                    int idx = it - history.begin();
                    px = gx + (int)((double)idx / std::max(1, ((int)history.size() - 1)) * gw);
                } else {
                    px = gx + (int)((double)i / (N - 1) * gw);
                }
                double bval = bmi_history[i].bmi;
                int py = gy + gh - (int)((bval - minb) / (maxb - minb) * gh);
                fl_pie(px - 3, py - 3, 6, 6, 0, 360);
                if (i > 0) fl_line(prevx, prevy, px, py);
                prevx = px; prevy = py;
            }
        }

        // Legends and axis labels
        fl_color(FL_BLACK);
        std::string ylab = (unit == UNIT_KG) ? "Weight (kg)" : "Weight (lbs)";
        fl_draw(ylab.c_str(), gx - 5, gy - 10);
        fl_draw("BMI", gx + gw + 6, gy - 10);
        fl_draw("Date", gx + gw / 2 - 15, gy + gh + 28);

        // Legend
        int legx = gx + 10, legy = gy + gh + 36;
        fl_color(FL_RED); fl_rectf(legx, legy, 12, 12); fl_color(FL_BLACK);
        fl_draw("Weight", legx + 16, legy + 11);
        fl_color(FL_DARK_BLUE); fl_rectf(legx + 80, legy, 12, 12); fl_color(FL_BLACK);
        fl_draw("BMI", legx + 96, legy + 11);

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

    // --- BMI tracking ---
    BMIDialog* bmi_dialog = nullptr;
    std::vector<BMIEntry> bmi_history;

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
            "- Targets are supported and saved/loaded as well.\n"
            "- Use 'Tools/Calculate BMI' to open the BMI dialog. Save/load BMI history via the dialog."
            "\n"
            "Created by Josh Conner (c)2025"
        );
    }

    // --- BMI menu callback
    static void bmi_menu_cb(Fl_Widget*, void* userdata) {
        WeightTracker* self = (WeightTracker*)userdata;
        self->show_bmi_dialog();
    }

    void show_bmi_dialog() {
        double last_weight = history.empty() ? 0.0 : history.back().weight;
        if (bmi_dialog) {
            bmi_dialog->show();
            bmi_dialog->take_focus();
            return;
        }
        bmi_dialog = new BMIDialog(last_weight, bmi_history, DEFAULT_BMI_FILE);
        bmi_dialog->show();
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
        int gw = 560, gh = 360;
        graph_window = new Fl_Window(gw, gh, "Weight & BMI History Graph");
        graph_widget = new WeightGraph(20, 20, gw - 40, gh - 40, history, target_weight, target_unit, graph_unit, bmi_history);
        graph_window->end();
        graph_window->set_non_modal();
        graph_window->show();
    }

    // --- BMI file loading for main app at startup (optional) ---
    void load_bmi_history_on_start() {
        std::ifstream ifs(DEFAULT_BMI_FILE, std::ios::binary);
        if (!ifs) return;
        size_t count = 0;
        if (!ifs.read(reinterpret_cast<char*>(&count), sizeof(count))) return;
        bmi_history.clear();
        for (size_t i = 0; i < count; ++i) {
            BMIEntry entry;
            if (!entry.load(ifs)) return;
            bmi_history.push_back(entry);
        }
    }

public:
    WeightTracker(int w, int h, const char* title = 0)
        : Fl_Window(w, h, title)
    {
        // Load BMI history on startup
        load_bmi_history_on_start();

        begin();
        menu_bar = new Fl_Menu_Bar(0, 0, w, 25);
        menu_bar->add("&Help/About", 0, help_menu_cb, this);
        menu_bar->add("&View/Weight Graph", 0, graph_menu_cb, this);
        menu_bar->add("&Tools/Calculate BMI", 0, bmi_menu_cb, this);

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
    WeightTracker wt(560, 360, "Weight Tracker 2.1.1");
    wt.show(argc, argv);
    return Fl::run();
}