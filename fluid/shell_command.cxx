//
// FLUID main entry for the Fast Light Tool Kit (FLTK).
//
// Copyright 1998-2021 by Bill Spitzak and others.
//
// This library is free software. Distribution and use rights are outlined in
// the file "COPYING" which should have been included with this file.  If this
// file is missing or damaged, see the license at:
//
//     https://www.fltk.org/COPYING.php
//
// Please see the following page on how to report bugs and issues:
//
//     https://www.fltk.org/bugs.php
//

#include "shell_command.h"

#include "fluid.h"
#include "alignment_panel.h"

#include <FL/Fl_Double_Window.H>
#include <FL/fl_message.H>

#include <errno.h>

static Fl_Process s_proc;

/** \class Fl_Process
 \todo Explain.
 */

Fl_Process::Fl_Process() {
  _fpt= NULL;
}

Fl_Process::~Fl_Process() {
  if (_fpt) close();
}

// FIXME: popen needs the UTF-8 equivalent fl_popen
// portable open process:
FILE * Fl_Process::popen(const char *cmd, const char *mode) {
#if defined(_WIN32)  && !defined(__CYGWIN__)
  // PRECONDITIONS
  if (!mode || !*mode || (*mode!='r' && *mode!='w') ) return NULL;
  if (_fpt) close(); // close first before reuse

  ptmode = *mode;
  pin[0] = pin[1] = pout[0] = pout[1] = perr[0] = perr[1] = INVALID_HANDLE_VALUE;
  // stderr to stdout wanted ?
  int fusion = (strstr(cmd,"2>&1") !=NULL);

  // Create windows pipes
  if (!createPipe(pin) || !createPipe(pout) || (!fusion && !createPipe(perr) ) )
    return freeHandles(); // error

  // Initialize Startup Info
  ZeroMemory(&si, sizeof(STARTUPINFO));
  si.cb           = sizeof(STARTUPINFO);
  si.dwFlags    = STARTF_USESTDHANDLES;
  si.hStdInput    = pin[0];
  si.hStdOutput   = pout[1];
  si.hStdError  = fusion ? pout[1] : perr [1];

  if ( CreateProcess(NULL, (LPTSTR) cmd,NULL,NULL,TRUE,
                     DETACHED_PROCESS,NULL,NULL, &si, &pi)) {
    // don't need theses handles inherited by child process:
    clean_close(pin[0]); clean_close(pout[1]); clean_close(perr[1]);
    HANDLE & h = *mode == 'r' ? pout[0] : pin[1];
    _fpt = _fdopen(_open_osfhandle((fl_intptr_t) h,_O_BINARY),mode);
    h= INVALID_HANDLE_VALUE;  // reset the handle pointer that is shared
    // with _fpt so we don't free it twice
  }

  if (!_fpt)  freeHandles();
  return _fpt;
#else
  _fpt=::popen(cmd,mode);
  return _fpt;
#endif
}

int Fl_Process::close() {
#if defined(_WIN32)  && !defined(__CYGWIN__)
  if (_fpt) {
    fclose(_fpt);
    clean_close(perr[0]);
    clean_close(pin[1]);
    clean_close(pout[0]);
    _fpt = NULL;
    return 0;
  }
  return -1;
#else
  int ret = ::pclose(_fpt);
  _fpt=NULL;
  return ret;
#endif
}

// non-null if file is open
FILE *Fl_Process::desc() const {
  return _fpt;
}

char *Fl_Process::get_line(char * line, size_t s) const {
  return _fpt ? fgets(line, (int)s, _fpt) : NULL;
}

// returns fileno(FILE*):
// (file must be open, i.e. _fpt must be non-null)
// *FIXME* we should find a better solution for the 'fileno' issue
// non null if file is open
int Fl_Process::get_fileno() const {
#ifdef _MSC_VER
    return _fileno(_fpt); // suppress MSVC warning
#else
    return fileno(_fpt);
#endif
}

#if defined(_WIN32)  && !defined(__CYGWIN__)

bool Fl_Process::createPipe(HANDLE * h, BOOL bInheritHnd) {
  SECURITY_ATTRIBUTES sa;
  sa.nLength = sizeof(sa);
  sa.lpSecurityDescriptor = NULL;
  sa.bInheritHandle = bInheritHnd;
  return CreatePipe (&h[0],&h[1],&sa,0) ? true : false;
}

FILE *Fl_Process::freeHandles()  {
  clean_close(pin[0]);    clean_close(pin[1]);
  clean_close(pout[0]);   clean_close(pout[1]);
  clean_close(perr[0]);   clean_close(perr[1]);
  return NULL; // convenient for error management
}

void Fl_Process::clean_close(HANDLE& h) {
  if (h!= INVALID_HANDLE_VALUE) CloseHandle(h);
  h = INVALID_HANDLE_VALUE;
}

#endif


// Shell command support...

static bool prepare_shell_command(const char * &command)  { // common pre-shell command code all platforms
  shell_window->hide();
  if (s_proc.desc()) {
    fl_alert("Previous shell command still running!");
    return false;
  }
  if ((command = shell_command_input->value()) == NULL || !*command) {
    fl_alert("No shell command entered!");
    return false;
  }
  if (shell_savefl_button->value()) {
    save_cb(0, 0);
  }
  if (shell_writecode_button->value()) {
    write_code_files();
  }
  if (shell_writemsgs_button->value()) {
    write_strings_cb(0, 0);
  }
  return true;
}

// Support the full piped shell command...
void shell_pipe_cb(FL_SOCKET, void*) {
  char  line[1024]="";          // Line from command output...

  if (s_proc.get_line(line, sizeof(line)) != NULL) {
    // Add the line to the output list...
    shell_run_terminal->append(line);
  } else {
    // End of file; tell the parent...
    Fl::remove_fd(s_proc.get_fileno());
    s_proc.close();
    shell_run_terminal->append("... END SHELL COMMAND ...\n");
  }
}

void do_shell_command(Fl_Return_Button*, void*) {
  const char    *command=NULL;  // Command to run

  if (!prepare_shell_command(command)) return;

  // Show the output window and clear things...
  shell_run_terminal->text("");
  shell_run_terminal->append(command);
  shell_run_terminal->append("\n");
  shell_run_window->label("Shell Command Running...");

  if (s_proc.popen((char *)command) == NULL) {
    fl_alert("Unable to run shell command: %s", strerror(errno));
    return;
  }

  shell_run_button->deactivate();

  Fl_Preferences pos(fluid_prefs, "shell_run_Window_pos");
  int x, y, w, h;
  pos.get("x", x, -1);
  pos.get("y", y, 0);
  pos.get("w", w, 640);
  pos.get("h", h, 480);
  if (x!=-1) {
    shell_run_window->resize(x, y, w, h);
  }
  shell_run_window->show();

  Fl::add_fd(s_proc.get_fileno(), shell_pipe_cb);

  while (s_proc.desc()) Fl::wait();

  shell_run_button->activate();
  shell_run_window->label("Shell Command Complete");
  fl_beep();

  while (shell_run_window->shown()) Fl::wait();
}

/**
 Show a dialog box to run an external shell command.
 */
void show_shell_window() {
  shell_window->hotspot(shell_command_input);
  shell_window->show();
}
