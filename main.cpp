#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Progress.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <thread>
#include <filesystem>
#include <iostream>
#include <vector>

// Declare the game launcher from Doom.cpp
extern "C" int run_game();

Fl_Progress* progress;
Fl_Box* status;
Fl_Window* win;

void load_and_launch(Fl_Widget*, void*) {
    std::vector<std::string> objFiles, pngFiles;

    for (const auto& entry : std::filesystem::directory_iterator(".")) {
        if (entry.path().extension() == ".obj") objFiles.push_back(entry.path().string());
        if (entry.path().extension() == ".png") pngFiles.push_back(entry.path().string());
    }

    int total = objFiles.size() + pngFiles.size();
    if (total == 0) {
        status->label("No .obj or .png files found!");
        return;
    }

    progress->maximum(total);
    status->label("Loading assets...");

    std::thread([=]() {
        int i = 0;
        for (const auto& f : objFiles) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            progress->value(++i);
            Fl::check();
        }
        for (const auto& f : pngFiles) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            progress->value(++i);
            Fl::check();
        }

        status->label("Launching game...");
        std::this_thread::sleep_for(std::chrono::milliseconds(800));

        win->hide();
        run_game();  // Launch game from doom.cpp
        exit(0);     // Exit after game closes
    }).detach();
}

int main(int argc, char** argv) {
    win = new Fl_Window(400, 200, "DoomQuake Launcher");

    status = new Fl_Box(20, 20, 360, 30, "Ready to load .obj/.png");
    progress = new Fl_Progress(20, 60, 360, 30);
    progress->minimum(0);
    progress->maximum(100);
    progress->value(0);

    Fl_Button* loadBtn = new Fl_Button(120, 120, 80, 30, "Load Game");
    loadBtn->callback(load_and_launch);

    win->end();
    win->show();

    return Fl::run();
}
