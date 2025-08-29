#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/Fl_Multiline_Output.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Progress.H>
#include <zip.h>
#include <fstream>
#include <vector>
#include <string>
#include <iostream>
#include <sys/stat.h>
#include <filesystem>

namespace fs = std::filesystem;

// GUI globals
Fl_Multiline_Output* file_list;
Fl_Input* output_zip;
Fl_Box* status_box;
Fl_Progress* progress_bar;
std::vector<std::string> selected_files;
std::string base_folder;

// progress + cancel
static zip_uint64_t total_bytes = 0;
static zip_uint64_t processed_bytes = 0;
static bool cancel_requested = false;  // 🚨 new flag

// ================== ZIP CREATION ==================
struct file_context {
    std::ifstream* infile;
    zip_uint64_t size;
    zip_uint64_t read_bytes;
};

zip_int64_t file_source_callback(void* state, void* data, zip_uint64_t len, zip_source_cmd_t cmd) {
    file_context* ctx = (file_context*)state;
    switch (cmd) {
        case ZIP_SOURCE_OPEN:
            ctx->infile->clear();
            ctx->infile->seekg(0);
            ctx->read_bytes = 0;
            return 0;
        case ZIP_SOURCE_READ: {
            if (cancel_requested) return -1; // cancel gracefully

            ctx->infile->read((char*)data, len);
            std::streamsize bytes = ctx->infile->gcount();
            ctx->read_bytes += bytes;
            processed_bytes += bytes;

            double overall_progress = (double)processed_bytes / total_bytes;
            progress_bar->value(overall_progress);
            progress_bar->label((std::to_string((int)(overall_progress * 100)) + "%").c_str());
            Fl::check();

            return bytes;
        }
        case ZIP_SOURCE_CLOSE:
            return 0;
        case ZIP_SOURCE_STAT: {
            struct zip_stat* st = (struct zip_stat*)data;
            zip_stat_init(st);
            st->valid = ZIP_STAT_SIZE;
            st->size = ctx->size;
            return sizeof(*st);
        }
        case ZIP_SOURCE_FREE:
            delete ctx->infile;
            delete ctx;
            return 0;
        default:
            return -1;
    }
}

bool compress_files(const std::vector<std::string>& files, const char* zip_filename, std::string& error_msg) {
    int errorp;
    zip_t* archive = zip_open(zip_filename, ZIP_CREATE | ZIP_TRUNCATE, &errorp);
    if (!archive) {
        error_msg = "Failed to create zip archive.";
        return false;
    }

    // compute total size
    total_bytes = 0;
    processed_bytes = 0;
    for (auto& file : files) {
        struct stat st;
        if (stat(file.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
            total_bytes += st.st_size;
        }
    }

    for (const auto& file : files) {
        if (cancel_requested) {
            zip_discard(archive);
            error_msg = "Compression cancelled by user.";
            return false;
        }

        struct stat st;
        if (stat(file.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) continue;

        file_context* ctx = new file_context;
        ctx->infile = new std::ifstream(file, std::ios::binary);
        if (!ctx->infile->is_open()) {
            error_msg = "Failed to open file: " + file;
            delete ctx->infile;
            delete ctx;
            zip_discard(archive);
            return false;
        }
        ctx->size = st.st_size;
        ctx->read_bytes = 0;

        zip_source_t* source = zip_source_function(archive, file_source_callback, ctx);
        if (!source) {
            error_msg = "Failed to create zip source for: " + file;
            delete ctx->infile;
            delete ctx;
            zip_discard(archive);
            return false;
        }

        fs::path rel_path;
        if (!base_folder.empty() && fs::exists(base_folder))
            rel_path = fs::relative(file, base_folder);
        else
            rel_path = fs::path(file).filename();

        if (zip_file_add(archive, rel_path.generic_string().c_str(), source,
                         ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8) < 0) {
            error_msg = "Error adding file: " + rel_path.string();
            zip_source_free(source);
            zip_discard(archive);
            return false;
        }
    }

    if (zip_close(archive) < 0) {
        error_msg = std::string("Error closing zip: ") + zip_strerror(archive);
        return false;
    }
    return true;
}

// ================== ZIP EXTRACTION ==================
bool extract_zip(const char* zip_filename, const char* dest_dir, std::string& error_msg) {
    int err = 0;
    zip_t* archive = zip_open(zip_filename, 0, &err);
    if (!archive) {
        error_msg = "Failed to open zip file.";
        return false;
    }

    zip_int64_t n_entries = zip_get_num_entries(archive, 0);

    // total size of all files
    total_bytes = 0;
    processed_bytes = 0;
    for (zip_int64_t i = 0; i < n_entries; i++) {
        struct zip_stat st;
        zip_stat_init(&st);
        if (zip_stat_index(archive, i, 0, &st) == 0) {
            total_bytes += st.size;
        }
    }

    for (zip_int64_t i = 0; i < n_entries; i++) {
        if (cancel_requested) {
            zip_close(archive);
            error_msg = "Extraction cancelled by user.";
            return false;
        }

        struct zip_stat st;
        zip_stat_init(&st);
        if (zip_stat_index(archive, i, 0, &st) != 0) continue;

        zip_file_t* zf = zip_fopen_index(archive, i, 0);
        if (!zf) {
            error_msg = "Failed to read file inside zip.";
            zip_close(archive);
            return false;
        }

        fs::path out_path = fs::path(dest_dir) / st.name;
        if (st.name[strlen(st.name) - 1] == '/') {
            fs::create_directories(out_path);
            zip_fclose(zf);
            continue;
        }

        fs::create_directories(out_path.parent_path());
        std::ofstream outfile(out_path, std::ios::binary);

        char buffer[4096];
        zip_uint64_t total = 0;
        while (total < st.size) {
            if (cancel_requested) {
                zip_fclose(zf);
                zip_close(archive);
                error_msg = "Extraction cancelled by user.";
                return false;
            }

            zip_int64_t bytes = zip_fread(zf, buffer, sizeof(buffer));
            if (bytes < 0) {
                error_msg = "Error extracting file.";
                zip_fclose(zf);
                zip_close(archive);
                return false;
            }
            outfile.write(buffer, bytes);
            total += bytes;
            processed_bytes += bytes;

            double overall_progress = (double)processed_bytes / total_bytes;
            progress_bar->value(overall_progress);
            progress_bar->label((std::to_string((int)(overall_progress * 100)) + "%").c_str());
            Fl::check();
        }

        outfile.close();
        zip_fclose(zf);
    }

    zip_close(archive);
    return true;
}

// ================== GUI CALLBACKS ==================
void cancel_cb(Fl_Widget*, void*) {
    cancel_requested = true;
    status_box->label("⏹ Cancel requested...");
}

void compress_cb(Fl_Widget*, void*) {
    cancel_requested = false;  // reset flag
    const char* zipname = output_zip->value();
    if (selected_files.empty() || !zipname || strlen(zipname) == 0) {
        status_box->label("⚠️ Please select files/folder and enter a zip name.");
        return;
    }

    progress_bar->value(0.0);
    progress_bar->label("0%");
    Fl::check();

    std::string error;
    if (compress_files(selected_files, zipname, error)) {
        if (!cancel_requested) {
            status_box->label("✅ Compression successful!");
            progress_bar->value(1.0);
            progress_bar->label("100%");
        }
    } else {
        status_box->label(("❌ " + error).c_str());
    }
}

void extract_cb(Fl_Widget*, void*) {
    cancel_requested = false; // reset flag

    Fl_File_Chooser zip_chooser(".", "*.zip", Fl_File_Chooser::SINGLE, "Select zip to extract");
    zip_chooser.show();
    while (zip_chooser.shown()) Fl::wait();
    if (!zip_chooser.value()) return;
    std::string zipfile = zip_chooser.value();

    Fl_File_Chooser folder_chooser(".", "*", Fl_File_Chooser::DIRECTORY, "Select destination folder");
    folder_chooser.show();
    while (folder_chooser.shown()) Fl::wait();
    if (!folder_chooser.value()) return;
    std::string dest = folder_chooser.value();

    progress_bar->value(0.0);
    progress_bar->label("0%");
    Fl::check();

    std::string error;
    if (extract_zip(zipfile.c_str(), dest.c_str(), error)) {
        if (!cancel_requested) {
            status_box->label("✅ Extraction successful!");
            progress_bar->value(1.0);
            progress_bar->label("100%");
        }
    } else {
        status_box->label(("❌ " + error).c_str());
    }
}

// ================== MAIN ==================
int main(int argc, char** argv) {
    Fl_Window* win = new Fl_Window(700, 420, "File/Folder Compressor & Extractor (ZIP)");

    Fl_Button* browse_files_btn = new Fl_Button(20, 20, 120, 30, "Browse Files");
    browse_files_btn->callback([](Fl_Widget*, void*) {
        Fl_File_Chooser chooser(".", "*", Fl_File_Chooser::MULTI, "Select files to compress");
        chooser.show();
        while (chooser.shown()) Fl::wait();
        if (chooser.count() > 0) {
            selected_files.clear();
            std::string list_display;
            base_folder.clear();
            for (int i = 1; i <= chooser.count(); i++) {
                selected_files.push_back(chooser.value(i));
                list_display += chooser.value(i);
                list_display += "\n";
            }
            file_list->value(list_display.c_str());
        }
    });

    Fl_Button* browse_folder_btn = new Fl_Button(160, 20, 120, 30, "Browse Folder");
    browse_folder_btn->callback([](Fl_Widget*, void*) {
        Fl_File_Chooser chooser(".", "*", Fl_File_Chooser::DIRECTORY, "Select folder to compress");
        chooser.show();
        while (chooser.shown()) Fl::wait();
        if (chooser.value()) {
            selected_files.clear();
            base_folder = chooser.value();
            std::string list_display;
            for (auto& p : fs::recursive_directory_iterator(base_folder)) {
                if (fs::is_regular_file(p)) {
                    selected_files.push_back(p.path().string());
                    list_display += p.path().string() + "\n";
                }
            }
            file_list->value(list_display.c_str());
        }
    });

    file_list = new Fl_Multiline_Output(150, 70, 500, 120, "Selected:");
    output_zip = new Fl_Input(150, 210, 300, 30, "Output .zip:");

    Fl_Button* compress_btn = new Fl_Button(150, 260, 120, 40, "Compress");
    compress_btn->callback(compress_cb);

    Fl_Button* extract_btn = new Fl_Button(300, 260, 120, 40, "Extract");
    extract_btn->callback(extract_cb);

    Fl_Button* cancel_btn = new Fl_Button(450, 260, 120, 40, "Cancel");
    cancel_btn->callback(cancel_cb);

    progress_bar = new Fl_Progress(150, 320, 300, 25, "Progress");
    progress_bar->minimum(0.0);
    progress_bar->maximum(1.0);
    progress_bar->value(0.0);
    progress_bar->color(FL_WHITE);
    progress_bar->selection_color(FL_BLUE);

    status_box = new Fl_Box(50, 360, 600, 30, "");

    win->end();
    win->show(argc, argv);
    return Fl::run();
}
