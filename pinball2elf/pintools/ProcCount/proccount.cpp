/*
 * Copyright (C) 2004-2021 Intel Corporation.
 * SPDX-License-Identifier: BSD-3-Clause
 */

//
// This tool counts the number of times a routine is executed and
// the number of instructions executed in a routine
//

#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>
#include <string.h>
#include <vector>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "pin.H"
// Replaced atomic.hpp with GCC built-in atomics

using std::cerr;
using std::dec;
using std::endl;
using std::hex;
using std::ofstream;
using std::setw;
using std::string;

KNOB<string> KnobOutDir(KNOB_MODE_WRITEONCE, "pintool", "outdir", ".", "Output directory");
KNOB<string> KnobMsg(KNOB_MODE_WRITEONCE, "pintool", "msgfile", "msg.out", "tool messages");
// Absolute outdir (set in main) so execve appends go to the right place even if cwd changes
static std::string AbsOutDir;

// Single place to resolve outdir (for msg path and execve_events). After exec, AbsOutDir is reset; -outdir knob is
// unchanged by exec so use it (script should pass absolute path so it resolves correctly).
static std::string get_outdir()
{
    std::string outdir = !AbsOutDir.empty() ? AbsOutDir : KnobOutDir.Value();
    if (!outdir.empty() && outdir.back() != '/') outdir += "/";
    return outdir;
}

// Append a line to path; return true if successful. (Used to detect msg path failures.)
static bool append_line_to_path_ok(const std::string& path, const std::string& line)
{
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) {
        write(fd, line.c_str(), line.size());
        write(fd, "\n", 1);
        close(fd);
        return true;
    }
    cerr << "TOOL: append_line_to_path failed path=" << path << " errno=" << errno << endl;
    return false;
}

// Append to outdir/msg.<pid>.txt and also to outdir/execve_events.txt (fixed name, so execve always visible).
// Uses get_outdir_for_pid so we write to the same dir as the ofstream reopen in OnSyscallExit.
static void append_msg_line(pid_t pid, const std::string& line)
{
    std::string outdir = get_outdir();
    std::string msg_path = outdir + "msg." + std::to_string(static_cast<unsigned long>(pid)) + ".txt";
    bool msg_ok = append_line_to_path_ok(msg_path, line);
    append_line_to_path_ok(outdir + "execve_events.txt", line);
    if (!msg_ok)
        append_line_to_path_ok(outdir + "execve_events.txt", std::string("TOOL: [failed to write to msg] ") + line);
}
// Optional: set both so child/exec-ed process gets -follow_execv and tool (needed on Linux for full chain).
KNOB<string> KnobPinPath(KNOB_MODE_WRITEONCE, "pintool", "pin_path", "", "Pin binary path (e.g. $PIN_ROOT) for child command line");
KNOB<string> KnobToolPath(KNOB_MODE_WRITEONCE, "pintool", "tool_path", "", "Tool .so path for child command line");
ofstream outFile;
ofstream msgFile;
BOOL proc_fini_called = FALSE;

// Holds instruction count for a single procedure
typedef struct RtnCount
{
    string _name;
    string _image;
    ADDRINT _address;
    RTN _rtn;
    UINT64 _rtnCount;
    UINT64 _icount;
    struct RtnCount* _next;
} RTN_COUNT;

// Linked list of instruction counts for each routine
RTN_COUNT* RtnList = 0;

typedef RTN_COUNT *RTN_COUNT_PTR;
typedef std::map<string, RTN_COUNT_PTR> RTNNAME_MAP;

RTNNAME_MAP RtnNameMap;

RTN_COUNT *OtherRC = NULL;

// This function is called before every instruction is executed
VOID docount(UINT64* counterptr, UINT32 value) 
{
    __sync_fetch_and_add(counterptr, value);
}
VOID docount_tid0(THREADID tid, char * rtnname,  UINT64* counterptr, UINT32 value) 
{
  if(tid==0)
  {
    __sync_fetch_and_add(counterptr, value);
    if(strcmp(rtnname,"floorf") == 0)
      outFile << "Incrementing for " << rtnname << endl;
  }
}
//{ (*counterptr) += value; }

const char* StripPath(const char* path)
{
    const char* file = strrchr(path, '/');
    if (file)
        return file + 1;
    else
        return path;
}

VOID InitOtherRoutine()
{
    OtherRC = new RTN_COUNT;
    OtherRC->_name     = "NORTN";
    OtherRC->_image    = "NOIMAGE";
    OtherRC->_address  = 0;
    OtherRC->_icount   = 0;
    OtherRC->_rtnCount = 0;
}

// Pin calls this function every time a new rtn is executed
VOID Routine(RTN rtn, VOID* v)
{
    if (RtnNameMap.find(RTN_Name(rtn)) == RtnNameMap.end())
    {
      // Allocate a counter for this routine
      RTN_COUNT* rc = new RTN_COUNT;

      // The RTN goes away when the image is unloaded, so save it now
      // because we need it in the fini
      rc->_name     = RTN_Name(rtn);
      rc->_image    = StripPath(IMG_Name(SEC_Img(RTN_Sec(rtn))).c_str());
      rc->_address  = RTN_Address(rtn);
      rc->_icount   = 0;
      rc->_rtnCount = 0;

      // Add to list of routines
      rc->_next = RtnList;
      RtnList   = rc;


    // Add to the map to find this later...
      RtnNameMap[ rc->_name ] = rc;

      //cerr << "Instrumenting RTN " << RTN_Name(rtn) << endl;
      RTN_Open(rtn);

    // Insert a call at the entry point of a routine to increment the call count
    RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)docount, IARG_PTR, &(rc->_rtnCount),IARG_UINT32, 1, IARG_END);
      //RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)docount_tid0, IARG_THREAD_ID, IARG_PTR, rc->_name.c_str(), IARG_PTR, &(rc->_rtnCount),IARG_UINT32, 1, IARG_END);

    RTN_Close(rtn);
   }
   else
   {
      //cerr << "SKIP Instrumenting RTN " << RTN_Name(rtn) << endl;
   }
}

// Pin calls this function every time a new basic block is encountered
// It inserts a call to docount

VOID Trace(TRACE trace, VOID* v)
{
    RTN_COUNT *rc = NULL;
    RTN rtn = TRACE_Rtn( trace );
    if (! RTN_Valid(rtn))
      rc = OtherRC;
    else
    {
      RTNNAME_MAP::const_iterator pos = RtnNameMap.find( RTN_Name(rtn) );
      if (pos == RtnNameMap.end())
        rc = OtherRC;
      else 
        rc = pos->second;
    }
    // Visit every basic block  in the trace
    for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl))
    {
        // Insert a call to docount for every bbl, passing the number of instructions.
        // IPOINT_ANYWHERE allows Pin to schedule the call anywhere in the bbl to obtain best performance.
        // Use a fast linkage for the call.
        BBL_InsertCall(bbl, IPOINT_BEFORE, (AFUNPTR)docount, IARG_PTR, &(rc->_icount), IARG_UINT32, BBL_NumIns(bbl), IARG_END);
    }
}

VOID OutPut(RTN_COUNT *rc)
{
  // UNDECORATION_NAME_ONLY
  // UNDECORATION_COMPLETE
  if (rc->_icount > 0)
    outFile << PIN_UndecorateSymbolName(rc->_name, UNDECORATION_NAME_ONLY)<< "#\t" << rc->_image << "#\t0x" << hex << rc->_address << dec
             << "#\t" << rc->_rtnCount << "#\t" << rc->_icount << endl;
}

VOID OutputProcCount()
{
    outFile << "Procedure#\t"
            << "Image#\t"
            << "Address#\t"
            << "Calls#\t"
            << "Instructions" << endl;

    OutPut(OtherRC);
    for (RTN_COUNT* rc = RtnList; rc; rc = rc->_next)
    {
      OutPut(rc);
    }
    outFile.close();
}

PIN_LOCK pinLock;

/* ===================================================================== */
/* Forward declarations                                                  */
/* ===================================================================== */

VOID ProcStart(pid_t pid);

/* ===================================================================== */
/* On Linux, exec-ed process may not get OnSyscallExit(execve); ensure   */
/* we run ProcStart when the main executable is loaded (fallback).      */
/* ===================================================================== */
VOID ImageLoad(IMG img, VOID* v)
{
    if (!IMG_IsMainExecutable(img)) return;
    if (outFile.is_open()) return;  // Already initialized (e.g. from OnSyscallExit or main)
    PIN_GetLock(&pinLock, PIN_GetPid());
    msgFile << "TOOL: ImageLoad(main) ProcStart fallback: pid=" << PIN_GetPid() << " " << IMG_Name(img) << endl;
    msgFile.flush();
    PIN_ReleaseLock(&pinLock);
    ProcStart(PIN_GetPid());
}

VOID ProcFini()
{
      PIN_GetLock(&pinLock, PIN_GetTid());
    if (!proc_fini_called)
    {
      proc_fini_called = TRUE;
      msgFile << "TOOL: Proc  Fini: pid=" << PIN_GetPid() << endl << std::flush;
      OutputProcCount();
    }
      PIN_ReleaseLock(&pinLock);
}

VOID OnSyscallEntry(THREADID threadIndex, CONTEXT* ctxt, SYSCALL_STANDARD std, VOID* v)
{
    ADDRINT sysnum = PIN_GetSyscallNumber(ctxt, std);
    if (sysnum == SYS_exit)
    {
      PIN_GetLock(&pinLock, threadIndex + 1);
      msgFile << "TOOL: At exit in pid=" << PIN_GetPid() << endl;
      msgFile.flush();
      PIN_ReleaseLock(&pinLock);
      //ProcFini();
    }
    if (sysnum == SYS_exit_group)
    {
      PIN_GetLock(&pinLock, threadIndex + 1);
      msgFile << "TOOL: At exit_group in pid=" << PIN_GetPid() << endl;
      msgFile.flush();
      PIN_ReleaseLock(&pinLock);
      //ProcFini();
    }
    if (sysnum == SYS_execve)
    {
      PIN_GetLock(&pinLock, threadIndex + 1);
      const char * pathname =  (const char *) PIN_GetSyscallArgument(ctxt, std, 0);
      pid_t pid = PIN_GetPid();
      cerr << "TOOL: At execve in " << PIN_GetTid() << " -> " << pathname << endl;
      cerr << "TOOL: execve entry pid=" << pid << " outdir=" << KnobOutDir.Value() << endl;
      // Use raw append so execve is always visible in msg file (ofstream can be invalid here)
      append_msg_line(pid, std::string("TOOL: At execve in pid=") + std::to_string(static_cast<unsigned long>(pid)) + " -> " + pathname);
      append_msg_line(pid, std::string("TOOL: Proc  Fini: pid=") + std::to_string(static_cast<unsigned long>(pid)));
      msgFile << "TOOL: At execve in pid=" << pid << endl;
      msgFile << "   ->" << pathname << endl;
      msgFile.flush();
      outFile << "EXECVE#"<< pid << "#" << pathname <<  "# 0" << "# 0" << endl;
      outFile.flush();
      PIN_ReleaseLock(&pinLock);
      ProcFini();
    }

}

VOID OnSyscallExit(THREADID threadIndex, CONTEXT* ctxt, SYSCALL_STANDARD std, VOID* v)
{
    ADDRINT sysnum = PIN_GetSyscallNumber(ctxt, std);
    if (sysnum == SYS_execve)
    {
      // After execve completes successfully, we're in a new process image
      // Reset state and start profiling the new process
      PIN_GetLock(&pinLock, threadIndex + 1);
      
      // Check if execve succeeded (return value >= 0 means success, but execve only returns on error)
      // Actually, execve only returns on error, so if we're here, execve succeeded
      // The process image has been replaced, so we need to reinitialize
      
      // Reset the proc_fini_called flag for the new process
      proc_fini_called = FALSE;
      
      // Clear the routine list (new process image means new routines)
      // Note: RtnList will be rebuilt as new routines are encountered
      // We don't need to delete the old list here as the process image is replaced
      
      // Reinitialize OtherRC for the new process
      InitOtherRoutine();
      
      // Start profiling the new process
      ProcStart(PIN_GetPid());
      
      // Reopen msgFile after exec: use same outdir as append_msg_line so we open the same file
      pid_t pid = PIN_GetPid();
      msgFile.close();
      std::string outdir = get_outdir();
      std::string msgPath = outdir + "msg." + std::to_string(static_cast<unsigned long>(pid)) + ".txt";
      msgFile.open(msgPath, std::ofstream::out | std::ofstream::app);
      const char * pathname =  (const char *) PIN_GetSyscallArgument(ctxt, std, 0);
      // Guarantee these appear in msg file via raw append (post-exec ofstream may be fresh)
      append_msg_line(pid, std::string("TOOL: msgFile reopened after exec (pid=") + std::to_string(static_cast<unsigned long>(pid)) + ")");
      append_msg_line(pid, std::string("TOOL: After execve, starting new process: pid=") + std::to_string(static_cast<unsigned long>(pid)) + " -> " + pathname);
      msgFile << "TOOL: msgFile reopened after exec (pid=" << pid << ")" << endl;
      msgFile << "TOOL: After execve, starting new process: pid=" << pid << " -> " << pathname << endl;
      msgFile.flush();
      outFile << "EXECVE_NEW#"<< pid << "#" << pathname <<  "# 0" << "# 0" << endl;
      PIN_ReleaseLock(&pinLock);
    }
}

VOID ProcStart(pid_t pid)
{
    PIN_GetLock(&pinLock, pid);
    msgFile << "TOOL: Proc Start: " << PIN_GetPid() << endl;
    msgFile.flush();  // shared with child; flush so output is visible
    char outFileName[ 15 + 10]; // strlen("/proccount.out")+1+10
    sprintf(outFileName, "/proccount.%d.out", pid);    
    string outdir = KnobOutDir.Value();
    //cerr << "outFileName " << (outdir+outFileName).c_str() << endl;
    if (outFile.is_open()) outFile.close(); // Might have inherited from the parent...
    outFile.open((outdir+outFileName).c_str(),  std::ofstream::out | std::ofstream::app);
    if(!outFile)
      cerr << "Failed to open outFileName " << (outdir+outFileName).c_str() << " errno " << std::strerror(errno) << endl;
        
    // Note: Syscall functions are registered once in main(), not per-process
    atexit(ProcFini);
    PIN_ReleaseLock(&pinLock);
}

/* ===================================================================== */

/* ===================================================================== */
/* Fork: parent-side callbacks. On Linux, Pin does not run in the       */
/* forked child (only exec is followed via FollowChild). We record fork */
/* events in the parent's output so consumers know a child was created  */
/* and is not instrumented. FPOINT_AFTER_IN_CHILD still runs if Pin     */
/* ever runs in the child (e.g. on some platforms).                      */
/* ===================================================================== */
VOID BeforeForkInParent(THREADID threadid, const CONTEXT* ctxt, VOID* arg)
{
    PIN_GetLock(&pinLock, threadid + 1);
    msgFile << "TOOL: Before fork in parent (on Linux, child will not be instrumented)" << endl;
    msgFile.flush();
    PIN_ReleaseLock(&pinLock);
}

VOID AfterForkInParent(THREADID threadid, const CONTEXT* ctxt, VOID* arg)
{
    PIN_GetLock(&pinLock, threadid + 1);
    pid_t parent_pid = PIN_GetPid();
    // On x86-64, fork() return value (child PID in parent) is in rax
    ADDRINT rax_val = PIN_GetContextReg(ctxt, REG_GAX);
    pid_t child_pid = (pid_t)(rax_val & 0x7FFFFFFF); // avoid sign extension / invalid
    if (child_pid <= 0)
    {
        msgFile << "TOOL: After fork in parent (fork may have failed, rax=" << rax_val << ")" << endl;
        msgFile.flush();
        PIN_ReleaseLock(&pinLock);
        return;
    }
    msgFile << "TOOL: After fork in parent: parent=" << parent_pid << " child=" << child_pid
            << " (child not instrumented on Linux)" << endl;
    msgFile.flush();
    outFile << "FORK#" << parent_pid << "#" << child_pid << "# 0" << "# 0" << endl;
    outFile.flush();
    PIN_ReleaseLock(&pinLock);
}

VOID AfterForkInChild(THREADID threadid, const CONTEXT* ctxt, VOID* arg)
{
    pid_t child_pid = PIN_GetPid();
    PIN_GetLock(&pinLock, threadid + 1);
    // Switch to child's own msg file in the output dir so parent's msg file is never touched by child
    msgFile.close();
    std::string path = KnobOutDir.Value();
    if (!path.empty() && path.back() != '/')
        path += "/";
    path += "msg." + std::to_string(static_cast<unsigned long>(child_pid)) + ".txt";
    msgFile.open(path);
    if (msgFile.is_open())
        msgFile << "TOOL: After fork in child pid=" << child_pid << endl;
    msgFile.flush();
    PIN_ReleaseLock(&pinLock);

    for (RTN_COUNT* rc = RtnList; rc; rc = rc->_next)
    {
       rc->_icount = 0;
    }
    ProcStart(child_pid);  // Use process ID so child gets proccount.<child_pid>.out
    // Write identity line so the forked child has its own proccount file with a clear marker
    PIN_GetLock(&pinLock, threadid + 1);
    outFile << "FORK_CHILD#" << child_pid << "# forked process# 0# 0" << endl;
    outFile.flush();
    PIN_ReleaseLock(&pinLock);
}

// This function is called when the application exits
// It prints the name and count for each procedure
VOID Fini(INT32 code, VOID* v)
{
    PIN_GetLock(&pinLock, PIN_GetPid());
    msgFile << "TOOL: Application   Fini: pid=" << PIN_GetPid() << endl << std::flush;
    PIN_ReleaseLock(&pinLock);
    msgFile.close();
    //ProcFini(); // main process finishes
}

/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */

INT32 Usage()
{
    cerr << "This Pintool counts the number of times a routine is executed" << endl;
    cerr << "and the number of instructions executed in a routine" << endl;
    cerr << endl << KNOB_BASE::StringKnobSummary() << endl;
    return -1;
}


/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */

int main(int argc, char* argv[])
{
    // Initialize symbol table code, needed for rtn instrumentation
    PIN_InitSymbols();


    // Initialize pin
    if (PIN_Init(argc, argv)) return Usage();

    string outdir = KnobOutDir.Value();
    if (mkdir(outdir.c_str(), 0777) == -1 && errno != EEXIST)
    {
      cerr << "Unable to create directory " << outdir << endl;
      exit(1);
    }
    // Convert to absolute so writes go to the same dir after exec (cwd may change). Prefer -outdir absolute from script.
    bool was_relative = !outdir.empty() && outdir[0] != '/';
    char abspath[4096];
    if (realpath(outdir.c_str(), abspath) != NULL) {
      AbsOutDir = abspath;
      if (was_relative)
        cerr << "TOOL: -outdir was relative, converted to absolute: " << AbsOutDir << endl;
    } else {
      AbsOutDir = outdir;
      if (was_relative)
        cerr << "TOOL: WARNING - -outdir was relative and realpath failed; output may go to wrong dir after exec: " << outdir << endl;
    }
    if (!AbsOutDir.empty() && AbsOutDir.back() != '/') AbsOutDir += "/";
    // Use outdir/msg.<pid>.txt per process so parent and child never share a path (avoids garbled parent file)
    std::string msgPath = AbsOutDir.empty() ? outdir : AbsOutDir;
    if (!msgPath.empty() && msgPath.back() != '/') msgPath += "/";
    msgPath += "msg." + std::to_string(static_cast<unsigned long>(PIN_GetPid())) + ".txt";
    msgFile.open(msgPath);
    msgFile << "TOOL: main: pid=" << PIN_GetPid() << endl << std::flush;
 
    // Fork callbacks: parent-side record fork (child not instrumented on Linux);
    // child-side reinit if Pin ever runs in the child.
    PIN_AddForkFunction(FPOINT_BEFORE, BeforeForkInParent, 0);
    PIN_AddForkFunction(FPOINT_AFTER_IN_PARENT, AfterForkInParent, 0);
    PIN_AddForkFunction(FPOINT_AFTER_IN_CHILD, AfterForkInChild, 0);

    // Ask Pin to inject into child/exec-ed processes (needed to follow pipeline workers).
    PIN_AddFollowChildProcessFunction(FollowChild, 0);

    InitOtherRoutine();

    ProcStart(PIN_GetPid()); // Main process starts; use PID for one file per process
    outFile << "MAIN#" << PIN_GetPid() << "#"; 
    bool dashseen = false;
    for (int i=0; i<argc; i++)
    {
      if(dashseen) outFile << argv[i] << " ";
      if(strcmp(argv[i],"--") == 0) dashseen=true;
    }
    outFile << "# 0" << "# 0" << endl;

    // Register syscall entry and exit functions
    PIN_AddSyscallEntryFunction(OnSyscallEntry, 0);
    PIN_AddSyscallExitFunction(OnSyscallExit, 0);

    // Fallback: exec-ed process on Linux may not get syscall exit; init on main image load.
    IMG_AddInstrumentFunction(ImageLoad, 0);

    // Register Routine to be called to instrument rtn
    RTN_AddInstrumentFunction(Routine, 0);

    // Register Trace to be called to instrument trace
    TRACE_AddInstrumentFunction(Trace, 0);

    // Register Fini to be called when the application exits
    PIN_AddFiniFunction(Fini, 0);

    // Start the program, never returns
    PIN_StartProgram();

    return 0;
}
