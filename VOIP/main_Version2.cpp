#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Box.H>
#include "voip_core.h"

Fl_Input* ip_input = nullptr;
Fl_Button* call_btn = nullptr;
Fl_Button* answer_btn = nullptr;
Fl_Button* hangup_btn = nullptr;
Fl_Box* status_box = nullptr;

VoipCore voip;

void call_cb(Fl_Widget*, void*) {
    std::string remote_ip = ip_input->value();
    status_box->label("Calling...");
    voip.start_call(remote_ip);
    status_box->label("In Call");
}

void answer_cb(Fl_Widget*, void*) {
    status_box->label("Waiting for call...");
    voip.answer_call();
    status_box->label("In Call");
}

void hangup_cb(Fl_Widget*, void*) {
    voip.hangup();
    status_box->label("Idle");
}

void close_cb(Fl_Widget*, void*) {
    voip.hangup();
    exit(0);
}

int main(int argc, char **argv) {
    Fl_Window* win = new Fl_Window(380, 200, "Robust VoIP (FLTK)");
    ip_input = new Fl_Input(120, 20, 220, 30, "Peer IP:");
    call_btn = new Fl_Button(40, 70, 80, 40, "Call");
    answer_btn = new Fl_Button(150, 70, 80, 40, "Answer");
    hangup_btn = new Fl_Button(260, 70, 80, 40, "Hang Up");
    status_box = new Fl_Box(80, 140, 220, 30, "Idle");

    call_btn->callback(call_cb);
    answer_btn->callback(answer_cb);
    hangup_btn->callback(hangup_cb);
    win->callback(close_cb);

    win->end();
    win->show(argc, argv);
    return Fl::run();
}