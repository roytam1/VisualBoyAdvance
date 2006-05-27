// VisualBoyAdvance - Nintendo Gameboy/GameboyAdvance (TM) emulator.
// Copyright (C) 1999-2003 Forgotten
// Copyright (C) 2005 Forgotten and the VBA development team

// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2, or(at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

// Parts adapted from VBA-H (VBA for Hackers) by LabMaster

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "../GBA.h"
#include "../Port.h"
#include "../armdis.h"
#include "../elf.h"
#include "../exprNode.h"

extern bool debugger;
extern int emulating;
extern void sdlWriteState(int num);
extern void sdlReadState(int num);

extern struct EmulatedSystem emulator;

#define debuggerReadMemory(addr) \
  READ32LE((&map[(addr)>>24].address[(addr) & map[(addr)>>24].mask]))

#define debuggerReadHalfWord(addr) \
  READ16LE((&map[(addr)>>24].address[(addr) & map[(addr)>>24].mask]))

#define debuggerReadByte(addr) \
  map[(addr)>>24].address[(addr) & map[(addr)>>24].mask]

#define debuggerWriteMemory(addr, value) \
  WRITE32LE(&map[(addr)>>24].address[(addr) & map[(addr)>>24].mask], value)

#define debuggerWriteHalfWord(addr, value) \
  WRITE16LE(&map[(addr)>>24].address[(addr) & map[(addr)>>24].mask], value)

#define debuggerWriteByte(addr, value) \
  map[(addr)>>24].address[(addr) & map[(addr)>>24].mask] = (value)

struct breakpointInfo {
  u32 address;
  u32 value;
  int size;

  u32 cond_address;
  char cond_rel;
  u32 cond_value;
  int cond_size;
  bool ia1;
  bool ia2;
};

struct DebuggerCommand {
  char *name;
  void (*function)(int,char **);
  char *help;
  char *syntax;
};

static void debuggerContinueAfterBreakpoint();

static void debuggerHelp(int,char **);
static void debuggerNext(int,char **);
static void debuggerContinue(int, char **);
static void debuggerRegisters(int, char **);
static void debuggerBreak(int, char **);
static void debuggerBreakDelete(int, char **);
static void debuggerBreakList(int, char **);
static void debuggerBreakArm(int, char **);
static void debuggerBreakThumb(int, char **);
static void debuggerBreakChange(int, char **);
static void debuggerBreakChangeClear(int, char **);
static void debuggerBreakWriteClear(int, char **);
static void debuggerBreakWrite(int, char **);
static void debuggerDebug(int, char **);
static void debuggerDisassemble(int, char **);
static void debuggerDisassembleArm(int, char **);
static void debuggerDisassembleThumb(int, char **);
static void debuggerEditByte(int, char **);
static void debuggerEditHalfWord(int, char **);
static void debuggerEditRegister(int, char **);
static void debuggerEdit(int, char **);
static void debuggerFileDisassemble(int, char **);
static void debuggerFileDisassembleArm(int, char **);
static void debuggerFileDisassembleThumb(int, char **);
static void debuggerIo(int, char **);
static void debuggerLocals(int, char **);
static void debuggerMemoryByte(int, char **);
static void debuggerMemoryHalfWord(int, char **);
static void debuggerMemory(int, char **);
static void debuggerPrint(int, char **);
static void debuggerQuit(int, char **);
static void debuggerSetRadix(int, char **);
static void debuggerSymbols(int, char **);
static void debuggerVerbose(int, char **);
static void debuggerWhere(int, char **);

static void debuggerReadState(int, char **);
static void debuggerWriteState(int, char **);
static void debuggerDumpLoad(int, char**);
static void debuggerDumpSave(int, char**);
static void debuggerCondValidate(int n, char **args, int start);
static bool debuggerCondEvaluate(int num);
static void debuggerCondBreakThumb(int, char **);
static void debuggerCondBreakArm(int, char **);

static DebuggerCommand debuggerCommands[] = {
  { "?", debuggerHelp,        "Show this help information. Type ? <command> for command help", "[<command>]" },
  { "ba", debuggerBreakArm,   "Add an ARM breakpoint", "<address>" },
  { "bd", debuggerBreakDelete,"Delete a breakpoint", "<number>" },
  { "bl", debuggerBreakList,  "List breakpoints" },
  { "bpc", debuggerBreakChange, "Break on change", "<address> <size>" },
  { "bpcc", debuggerBreakChangeClear, "Clear break on change", "[<address> <size>]" },
  { "bpw", debuggerBreakWrite, "Break on write", "<address> <size>" },
  { "bpwc", debuggerBreakWriteClear, "Clear break on write", "[<address> <size>]" },
  { "break", debuggerBreak,    "Add a breakpoint on the given function", "<function>|<line>|<file:line>" },
  { "bt", debuggerBreakThumb, "Add a THUMB breakpoint", "<address>" },
  { "c", debuggerContinue,    "Continue execution" , NULL },
  { "cba", debuggerCondBreakArm, "Add a conditional ARM breakpoint", "<address> $<address>|R<register> <comp> <value> [<size>]\n<comp> either ==, !=, <, >, <=, >=\n<size> either b, h, w" },
  { "cbt", debuggerCondBreakThumb, "Add a conditional THUMB breakpoint", "<address> $<address>|R<register> <comp> <value> [<size>]\n<comp> either ==, !=, <, >, <=, >=\n<size> either b, h, w" },
  { "d", debuggerDisassemble, "Disassemble instructions", "[<address> [<number>]]" },
  { "da", debuggerDisassembleArm, "Disassemble ARM instructions", "[<address> [<number>]]" },
  { "dload",debuggerDumpLoad, "Load raw data dump from file","<file> <address>"},
  { "dsave",debuggerDumpSave, "Dump raw data to file","<file> <address> <size>"},
  { "dt", debuggerDisassembleThumb, "Disassemble THUMB instructions", "[<address> [<number>]]" },
  { "eb", debuggerEditByte,   "Modify memory location (byte)", "<address> <hex value>" },
  { "eh", debuggerEditHalfWord,"Modify memory location (half-word)","<address> <hex value>" },
  { "er", debuggerEditRegister,       "Modify register", "<register number> <hex value>" },
  { "ew", debuggerEdit,       "Modify memory location (word)", "<address> <hex value>" },
  { "fd", debuggerFileDisassemble, "Disassemble instructions to file", "<file> [<address> [<number>]]" },
  { "fda", debuggerFileDisassembleArm, "Disassemble ARM instructions to file", "<file> [<address> [<number>]]" },
  { "fdt", debuggerFileDisassembleThumb, "Disassemble THUMB instructions to file", "<file> [<address> [<number>]]" },
  { "h", debuggerHelp,        "Show this help information. Type h <command> for command help", "[<command>]" },
  { "io", debuggerIo,         "Show I/O registers status", "[video|video2|dma|timer|misc]" },
  { "load", debuggerReadState,	"Load a savegame", "<number>" },
  { "locals", debuggerLocals, "Show local variables", NULL },
  { "mb", debuggerMemoryByte, "Show memory contents (bytes)", "<address>" },
  { "mh", debuggerMemoryHalfWord, "Show memory contents (half-words)", "<address>"},
  { "mw", debuggerMemory,     "Show memory contents (words)", "<address>" },
  { "n", debuggerNext,        "Execute the next instruction", "[<count>]" },
  { "print", debuggerPrint,   "Print the value of a expression (if known)", "[/x|/o|/d] <expression>" },
  { "q", debuggerQuit,        "Quit the emulator", NULL },
  { "r", debuggerRegisters,   "Show ARM registers", NULL },
  { "radix", debuggerSetRadix,   "Set the print radix", "<radix>" },
  { "save", debuggerWriteState,	"Create a savegame", "<number>" },
  { "symbols", debuggerSymbols, "List symbols", "[<symbol>]" },
#ifndef FINAL_VERSION
  { "trace", debuggerDebug,       "Set the trace level", "<value>" },
#endif
#ifdef DEV_VERSION
  { "verbose", debuggerVerbose,     "Change verbose setting", "<value>" },
#endif
  { "where", debuggerWhere,   "Show the call chain (if available)", NULL },
  { NULL, NULL, NULL, NULL} // end marker
};

breakpointInfo debuggerBreakpointList[100];

int debuggerNumOfBreakpoints = 0;
bool debuggerAtBreakpoint = false;
int debuggerBreakpointNumber = 0;
int debuggerRadix = 0;

extern u32 cpuPrefetch[2];

#define ARM_PREFETCH \
  {\
    cpuPrefetch[0] = debuggerReadMemory(armNextPC);\
    cpuPrefetch[1] = debuggerReadMemory(armNextPC+4);\
  }

#define THUMB_PREFETCH \
  {\
    cpuPrefetch[0] = debuggerReadHalfWord(armNextPC);\
    cpuPrefetch[1] = debuggerReadHalfWord(armNextPC+2);\
  }

static void debuggerPrefetch()
{
  if(armState) {
    ARM_PREFETCH;
  } else {
    THUMB_PREFETCH;
  }
}

static void debuggerApplyBreakpoint(u32 address, int num, int size)
{
  if(size)
    debuggerWriteMemory(address, (u32)(0xe1200070 | 
                                       (num & 0xf) | 
                                       ((num<<4)&0xf0)));
  else
    debuggerWriteHalfWord(address, 
                          (u16)(0xbe00 | num));
}

static void debuggerDisableBreakpoints()
{
  for(int i = 0; i < debuggerNumOfBreakpoints; i++) {
    if(debuggerBreakpointList[i].size)
      debuggerWriteMemory(debuggerBreakpointList[i].address,
                          debuggerBreakpointList[i].value);
    else
      debuggerWriteHalfWord(debuggerBreakpointList[i].address,
                            debuggerBreakpointList[i].value);      
  }
}

static void debuggerEnableBreakpoints(bool skipPC)
{
  for(int i = 0; i < debuggerNumOfBreakpoints; i++) {
    if(debuggerBreakpointList[i].address == armNextPC && skipPC)
      continue;

    debuggerApplyBreakpoint(debuggerBreakpointList[i].address,
                            i,
                            debuggerBreakpointList[i].size);
  }  
}

static void debuggerUsage(char *cmd)
{
  for(int i = 0; ; i++) {
    if(debuggerCommands[i].name) {
      if(!strcmp(debuggerCommands[i].name, cmd)) {
        printf("%s %s\n\n%s\n", 
               debuggerCommands[i].name, 
               debuggerCommands[i].syntax ? debuggerCommands[i].syntax : "",
               debuggerCommands[i].help);
        break;
      }
    } else {
      printf("Unrecognized command '%s'.", cmd);
      break;
    }
  }  
}

static void debuggerPrintBaseType(Type *t, u32 value, u32 location,
                                  LocationType type,
                                  int bitSize, int bitOffset)
{
  if(bitSize) {
    if(bitOffset)
      value >>= ((t->size*8)-bitOffset-bitSize);
    value &= (1 << bitSize)-1;
  } else {
    if(t->size == 2)
      value &= 0xFFFF;
    else if(t->size == 1)
      value &= 0xFF;
  }

  if(t->size == 8) {
    u64 value = 0;
    if(type == LOCATION_memory) {
      value = debuggerReadMemory(location) |
        ((u64)debuggerReadMemory(location+4)<<32);
    } else if(type == LOCATION_register) {
      value = reg[location].I | ((u64)reg[location+1].I << 32);
    }
    switch(t->encoding) {
    case DW_ATE_signed:
      switch(debuggerRadix) {
      case 0:
        printf("%lld", value);
        break;
      case 1:
        printf("0x%llx", value);
        break;
      case 2:
        printf("0%llo", value);
        break;
      }
      break;
    case DW_ATE_unsigned:
      switch(debuggerRadix) {
      case 0:
        printf("%llu", value);
        break;
      case 1:
        printf("0x%llx", value);
        break;
      case 2:
        printf("0%llo", value);
        break;
      }
      break;
    default:
      printf("Unknowing 64-bit encoding\n");
    }
    return;
  }
  
  switch(t->encoding) {
  case DW_ATE_boolean:
    if(value)
      printf("true");
    else
      printf("false");
    break;
  case DW_ATE_signed:
    switch(debuggerRadix) {
    case 0:
      printf("%d", value);
      break;
    case 1:
      printf("0x%x", value);
      break;
    case 2:
      printf("0%o", value);
      break;
    }
    break;
  case DW_ATE_unsigned:
  case DW_ATE_unsigned_char:
    switch(debuggerRadix) {
    case 0:
      printf("%u", value);
      break;
    case 1:
      printf("0x%x", value);
      break;
    case 2:
      printf("0%o", value);
      break;
    }
    break;
  default:
    printf("UNKNOWN BASE %d %08x", t->encoding, value);
  }
}

static char *debuggerPrintType(Type *t)
{
  char buffer[1024];  
  static char buffer2[1024];
  
  if(t->type == TYPE_pointer) {
    if(t->pointer)
      strcpy(buffer, debuggerPrintType(t->pointer));
    else
      strcpy(buffer, "void");
    sprintf(buffer2, "%s *", buffer);
    return buffer2;
  } else if(t->type == TYPE_reference) {
    strcpy(buffer, debuggerPrintType(t->pointer));
    sprintf(buffer2, "%s &", buffer);
    return buffer2;    
  }
  return t->name;
}

static void debuggerPrintValueInternal(Function *, Type *, ELFBlock *, int, int, u32);
static void debuggerPrintValueInternal(Function *f, Type *t,
				       int bitSize, int bitOffset,
				       u32 objLocation, LocationType type);

static u32 debuggerGetValue(u32 location, LocationType type)
{
  switch(type) {
  case LOCATION_memory:
    return debuggerReadMemory(location);
  case LOCATION_register:
    return reg[location].I;
  case LOCATION_value:
    return location;
  }
  return 0;
}

static void debuggerPrintPointer(Type *t, u32 value)
{
  printf("(%s)0x%08x", debuggerPrintType(t), value);
}

static void debuggerPrintReference(Type *t, u32 value)
{
  printf("(%s)0x%08x", debuggerPrintType(t), value);
}

static void debuggerPrintFunction(Type *t, u32 value)
{
  printf("(%s)0x%08x", debuggerPrintType(t), value);
}

static void debuggerPrintArray(Type *t, u32 value)
{
  // todo
  printf("(%s[])0x%08x", debuggerPrintType(t->array->type), value);
}

static void debuggerPrintMember(Function *f,
                                Member *m,
                                u32 objLocation,
                                u32 location)
{
  int bitSize = m->bitSize;
  if(bitSize) {
    u32 value = 0;
    int off = m->bitOffset;
    int size = m->byteSize;
    u32 v = 0;
    if(size == 1)
      v = debuggerReadByte(location);
      else if(size == 2)
        v = debuggerReadHalfWord(location);
      else if(size == 4)
        v = debuggerReadMemory(location);
      
      while(bitSize) {
        int top = size*8 - off;
        int bot = top - bitSize;
        top--;
        if(bot >= 0) {
          value = (v >> (size*8 - bitSize - off)) & ((1 << bitSize)-1);
          bitSize = 0;
        } else {
          value |= (v & ((1 << top)-1)) << (bitSize - top);
          bitSize -= (top+1);
          location -= size;
          off = 0;
          if(size == 1)
            v = debuggerReadByte(location);
          else if(size == 2)
            v = debuggerReadHalfWord(location);
          else
            v = debuggerReadMemory(location);
        }
      }
      debuggerPrintBaseType(m->type, value, location, LOCATION_memory,
                            bitSize, 0);
    } else {
      debuggerPrintValueInternal(f, m->type, m->location, m->bitSize,
                                 m->bitOffset, objLocation);
    }  
}

static void debuggerPrintStructure(Function *f, Type *t, u32 objLocation)
{
  printf("{");
  int count = t->structure->memberCount;
  int i = 0;
  while(i < count) {
    Member *m = &t->structure->members[i];
    printf("%s=", m->name);
    LocationType type;
    u32 location = elfDecodeLocation(f, m->location, &type, objLocation);
    debuggerPrintMember(f, m, objLocation, location);
    i++;
    if(i < count)
      printf(",");
  }
  printf("}");
}

static void debuggerPrintUnion(Function *f, Type *t, u32 objLocation)
{
  // todo
  printf("{");
  int count = t->structure->memberCount;
  int i = 0;
  while(i < count) {
    Member *m = &t->structure->members[i];
    printf("%s=", m->name);
    debuggerPrintMember(f, m, objLocation, 0);
    i++;
    if(i < count)
      printf(",");
  }
  printf("}");
}

static void debuggerPrintEnum(Type *t, u32 value)
{
  int i;
  for(i = 0; i < t->enumeration->count; i++) {
    EnumMember *m = (EnumMember *)&t->enumeration->members[i];
    if(value == m->value) {
      printf(m->name);
      return;
    }
  }
  printf("(UNKNOWN VALUE) %d", value);
}

static void debuggerPrintValueInternal(Function *f, Type *t,
                                       int bitSize, int bitOffset,
                                       u32 objLocation, LocationType type)
{
  u32 value = debuggerGetValue(objLocation, type);
  if(!t) {
    printf("void");
    return;
  }
  switch(t->type) {
  case TYPE_base:
    debuggerPrintBaseType(t, value, objLocation, type, bitSize, bitOffset);
    break;
  case TYPE_pointer:
    debuggerPrintPointer(t, value);
    break;
  case TYPE_reference:
    debuggerPrintReference(t, value);
    break;
  case TYPE_function:
    debuggerPrintFunction(t, value);
    break;
  case TYPE_array:
    debuggerPrintArray(t, objLocation);
    break;
  case TYPE_struct:
    debuggerPrintStructure(f, t, objLocation);
    break;
  case TYPE_union:
    debuggerPrintUnion(f, t, objLocation);
    break;
  case TYPE_enum:
    debuggerPrintEnum(t, value);
    break;
  default:
    printf("%08x", value);
    break;
  }  
}

static void debuggerPrintValueInternal(Function *f, Type *t, ELFBlock *loc,
                                       int bitSize, int bitOffset, 
                                       u32 objLocation)
{
  LocationType type;  
  u32 location;
  if(loc) {
    if(objLocation)
      location = elfDecodeLocation(f, loc, &type, objLocation);
    else
      location = elfDecodeLocation(f, loc,&type);
  } else {
    location = objLocation;
    type = LOCATION_memory;
  }

  debuggerPrintValueInternal(f, t, bitSize, bitOffset, location, type);
}

static void debuggerPrintValue(Function *f, Object *o)
{
  debuggerPrintValueInternal(f, o->type, o->location, 0, 0, 0);
  
  printf("\n");
}

static void debuggerSymbols(int argc, char **argv)
{
  int i = 0;
  u32 value;
  u32 size;
  int type;
  bool match = false;
  int matchSize = 0;
  char *matchStr = NULL;
  
  if(argc == 2) {
    match = true;
    matchSize = strlen(argv[1]);
    matchStr = argv[1];
  }
  printf("Symbol               Value    Size     Type   \n");
  printf("-------------------- -------  -------- -------\n");
  char *s = NULL;
  while((s = elfGetSymbol(i, &value, &size, &type))) {
    if(*s) {
      if(match) {
        if(strncmp(s, matchStr, matchSize) != 0) {
          i++;
          continue;
        }
      }
      char *ts = "?";
      switch(type) {
      case 2:
        ts = "ARM";
        break;
      case 0x0d:
        ts = "THUMB";
        break;
      case 1:
        ts = "DATA";
        break;
      }
      printf("%-20s %08x %08x %-7s\n",
             s, value, size, ts);
    }
    i++;
  }
}

static void debuggerSetRadix(int argc, char **argv)
{
  if(argc != 2)
    debuggerUsage(argv[0]);
  else {
    int r = atoi(argv[1]);

    bool error = false;
    switch(r) {
    case 10:
      debuggerRadix = 0;
      break;
    case 8:
      debuggerRadix = 2;
      break;
    case 16:
      debuggerRadix = 1;
      break;
    default:
      error = true;
      printf("Unknown radix %d. Valid values are 8, 10 and 16.\n", r);
      break;
    }
    if(!error)
      printf("Radix set to %d\n", r);
  }
}

static void debuggerPrint(int argc, char **argv)
{
  if(argc != 2 && argc != 3) {
    debuggerUsage(argv[0]);
  } else {
    u32 pc = armNextPC;
    Function *f = NULL;
    CompileUnit *u = NULL;
    
    elfGetCurrentFunction(pc,
                          &f, &u);

    int oldRadix = debuggerRadix;
    if(argc == 3) {
      if(argv[1][0] == '/') {
        if(argv[1][1] == 'x')
          debuggerRadix = 1;
        else if(argv[1][1] == 'o')
          debuggerRadix = 2;
        else if(argv[1][1] == 'd')
          debuggerRadix = 0;
        else {
          printf("Unknown format %c\n", argv[1][1]);
          return;
        }
      } else {
        printf("Unknown option %s\n", argv[1]);
        return;
      }
    } 
    
    char *s = argc == 2 ? argv[1] : argv[2];

    extern char *exprString;
    extern int exprCol;
    extern int yyparse();
    exprString = s;
    exprCol = 0;
    if(!yyparse()) {
      extern Node *result;
      if(result->resolve(result, f, u)) {
        if(result->member)
          debuggerPrintMember(f,
                              result->member,
                              result->objLocation,
                              result->location);
        else
          debuggerPrintValueInternal(f, result->type, 0, 0,
                                     result->location,
                                     result->locType);
        printf("\n");
      } else {
        printf("Error resolving expression\n");
      }
    } else {
      printf("Error parsing expression:\n");
      printf("%s\n", s);
      exprCol--;
      for(int i = 0; i < exprCol; i++)
        printf(" ");
      printf("^\n");
    }
    extern void exprCleanBuffer();
    exprCleanBuffer();
    exprNodeCleanUp();
    debuggerRadix = oldRadix;
  }
}

static void debuggerHelp(int n, char **args)
{
  if(n == 2) {
    debuggerUsage(args[1]);
  } else {
    for(int i = 0; ; i++) {
      if(debuggerCommands[i].name) {
        printf("%s\t%s\n", debuggerCommands[i].name, debuggerCommands[i].help);
      } else
        break;
    }
  }
}

static void debuggerDebug(int n, char **args)
{
  if(n == 2) {
    int v = 0;
    sscanf(args[1], "%d", &v);
    systemDebug = v;
    printf("Debug level set to %d\n", systemDebug);
  } else
    debuggerUsage("trace");      
}

static void debuggerVerbose(int n, char **args)
{
  if(n == 2) {
    int v = 0;
    sscanf(args[1], "%d", &v);
    systemVerbose = v;
    printf("Verbose level set to %d\n", systemVerbose);
  } else
    debuggerUsage("verbose");    
}

static void debuggerWhere(int n, char **args)
{
  void elfPrintCallChain(u32);
  elfPrintCallChain(armNextPC);
}

static void debuggerLocals(int n, char **args)
{
  Function *f = NULL;
  CompileUnit *u = NULL;
  u32 pc = armNextPC;
  if(elfGetCurrentFunction(pc,
                           &f, &u)) {
    Object *o = f->parameters;
    while(o) {
      printf("%s=", o->name);
      debuggerPrintValue(f, o);
      o = o->next;
    }

    o = f->variables;
    while(o) {
      bool visible = o->startScope ? pc>=o->startScope : true;
      if(visible)
        visible = o->endScope ? pc < o->endScope : true;
      if(visible) {
        printf("%s=", o->name);
        debuggerPrintValue(f, o);
      }
      o = o->next;      
    }
  } else {
    printf("No information for current address\n");
  }  
}

static void debuggerNext(int n, char **args)
{
  int count = 1;
  if(n == 2) {
    sscanf(args[1], "%d", &count);
  }
  for(int i = 0; i < count; i++) {
    if(debuggerAtBreakpoint) {
      debuggerContinueAfterBreakpoint();
      debuggerEnableBreakpoints(false);
    } else {
      debuggerPrefetch();
      emulator.emuMain(1);
    }
  }
  debuggerDisableBreakpoints();
  Function *f = NULL;
  CompileUnit *u = NULL;
  u32 a = armNextPC;
  if(elfGetCurrentFunction(a, &f, &u)) {
    char *file;
    int line = elfFindLine(u, f, a, &file);
    
    printf("File %s, function %s, line %d\n", file, f->name,
           line);
  }
  debuggerRegisters(0, NULL);
}

static void debuggerContinue(int n, char **args)
{
  if(debuggerAtBreakpoint)
    debuggerContinueAfterBreakpoint();
  debuggerEnableBreakpoints(false);
  debugger = false;
  debuggerPrefetch();
}

/*extern*/ void debuggerSignal(int sig,int number)
{
  switch(sig) {
  case 4:
    {
      printf("Illegal instruction at %08x\n", armNextPC);
      debugger = true;
    }
    break;
  case 5:
    {
      bool cond = debuggerCondEvaluate(number & 255);
      if(cond) {
        printf("Breakpoint %d reached\n", number);
        debugger = true;
      } else {
        debuggerDisableBreakpoints();
        debuggerPrefetch();
        emulator.emuMain(1);
        debuggerEnableBreakpoints(false);
        return;
      }
      debuggerAtBreakpoint = true;
      debuggerBreakpointNumber = number;
      debuggerDisableBreakpoints();

      Function *f = NULL;
      CompileUnit *u = NULL;
      
      if(elfGetCurrentFunction(armNextPC, &f, &u)) {
        char *file;
        int line = elfFindLine(u,f,armNextPC,&file);
        printf("File %s, function %s, line %d\n", file, f->name,
               line);
      }
    }
    break;
  default:
    printf("Unknown signal %d\n", sig);
    break;
  }
}

static void debuggerBreakList(int, char **)
{
  printf("Num Address  Type  Symbol\n");
  printf("--- -------- ----- ------\n");
  for(int i = 0; i < debuggerNumOfBreakpoints; i++) {
    printf("%3d %08x %s %s\n",i, debuggerBreakpointList[i].address,
           debuggerBreakpointList[i].size ? "ARM" : "THUMB",
           elfGetAddressSymbol(debuggerBreakpointList[i].address));
  }
}

static void debuggerBreakDelete(int n, char **args)
{
  if(n == 2) {
    int n = 0;
    sscanf(args[1], "%d", &n);
    if(n >= 0 && n < debuggerNumOfBreakpoints) {
      printf("Deleting breakpoint %d (%d)\n", n, debuggerNumOfBreakpoints);
      n++;
      if(n < debuggerNumOfBreakpoints) {
        for(int i = n; i < debuggerNumOfBreakpoints; i++) {
          debuggerBreakpointList[i-1].address = 
            debuggerBreakpointList[i].address;
          debuggerBreakpointList[i-1].value = 
            debuggerBreakpointList[i].value;
          debuggerBreakpointList[i-1].size = 
            debuggerBreakpointList[i].size;
        }
      }
      debuggerNumOfBreakpoints--;
    }
    else
      printf("No breakpoints are set\n");
  } else
    debuggerUsage("bd");    
}

static void debuggerBreak(int n, char **args)
{
  if(n == 2) {
    u32 address = 0;
    u32 value = 0;
    int type = 0;
    char *s = args[1];
    char c = *s;
    if(strchr(s, ':')) {
      char *name = s;
      char *l = strchr(s, ':');
      *l++ = 0;
      int line = atoi(l);

      u32 addr;
      Function *f;
      CompileUnit *u;
      
      if(elfFindLineInModule(&addr, name, line)) {
        if(elfGetCurrentFunction(addr, &f, &u)) {
          u32 addr2;
          if(elfGetSymbolAddress(f->name, &addr2, &value, &type)) {
            address = addr;
          } else {
            printf("Unable to get function symbol data\n");
            return;
          }
        } else {
          printf("Unable to find function for address\n");
          return;
        }
      } else {
        printf("Unable to find module or line\n");
        return;
      }
    } else if(c >= '0' && c <= '9') {
      int line = atoi(s);
      Function *f;
      CompileUnit *u;
      u32 addr;
      
      if(elfGetCurrentFunction(armNextPC, &f, &u)) {
        if(elfFindLineInUnit(&addr, u, line)) {
          if(elfGetCurrentFunction(addr, &f, &u)) {
            u32 addr2;
            if(elfGetSymbolAddress(f->name, &addr2, &value, &type)) {
              address = addr;
            } else {
              printf("Unable to get function symbol data\n");
              return;
            }
          } else {
            printf("Unable to find function for address\n");
            return;
          }
        } else {
          printf("Unable to find line\n");
          return;
        }
      } else {
        printf("Cannot find current function\n");
        return;
      }
    } else {
      if(!elfGetSymbolAddress(s, &address, &value, &type)) {
        printf("Function %s not found\n", args[1]);
        return;
      }
    }
    if(type == 0x02 || type == 0x0d) {
      int i = debuggerNumOfBreakpoints;
      int size = 0;
      if(type == 2)
        size = 1;
      debuggerBreakpointList[i].address = address;
      debuggerBreakpointList[i].value = type == 0x02 ?
        debuggerReadMemory(address) : debuggerReadHalfWord(address);
      debuggerBreakpointList[i].size = size;
      //      debuggerApplyBreakpoint(address, i, size);
      debuggerNumOfBreakpoints++;
      if(size)
        printf("Added ARM breakpoint at %08x\n", address);        
      else
        printf("Added THUMB breakpoint at %08x\n", address);
    } else {
      printf("%s is not a function symbol\n", args[1]); 
    }
  } else
    debuggerUsage("break");  
}

static void debuggerBreakThumb(int n, char **args)
{
  if(n == 2) {
    u32 address = 0;
    sscanf(args[1],"%x", &address);
    int i = debuggerNumOfBreakpoints;
    debuggerBreakpointList[i].address = address;
    debuggerBreakpointList[i].value = debuggerReadHalfWord(address);
    debuggerBreakpointList[i].size = 0;
    //    debuggerApplyBreakpoint(address, i, 0);
    debuggerNumOfBreakpoints++;
    printf("Added THUMB breakpoint at %08x\n", address);
  } else
    debuggerUsage("bt");    
}

static void debuggerBreakArm(int n, char **args)
{
  if(n == 2) {
    u32 address = 0;
    sscanf(args[1],"%x", &address);
    int i = debuggerNumOfBreakpoints;
    debuggerBreakpointList[i].address = address;
    debuggerBreakpointList[i].value = debuggerReadMemory(address);
    debuggerBreakpointList[i].size = 1;
    //    debuggerApplyBreakpoint(address, i, 1);
    debuggerNumOfBreakpoints++;
    printf("Added ARM breakpoint at %08x\n", address);
  } else
    debuggerUsage("ba");
}

/*extern*/ void debuggerBreakOnWrite(u32 address, u32 oldvalue, u32 value, 
                                     int size, int t)
{
  const char *type = "write";
  if(t == 2)
    type = "change";

  if(size == 2)
    printf("Breakpoint (on %s) address %08x old:%08x new:%08x\n", 
           type, address, oldvalue, value);
  else if(size == 1)
    printf("Breakpoint (on %s) address %08x old:%04x new:%04x\n", 
           type, address, (u16)oldvalue,(u16)value);
  else
    printf("Breakpoint (on %s) address %08x old:%02x new:%02x\n", 
           type, address, (u8)oldvalue, (u8)value);
  debugger = true;
}

static void debuggerBreakWriteClear(int n, char **args)
{
  if(n == 3) {
    u32 address = 0;
    sscanf(args[1], "%x", &address);
    int n = 0;
    sscanf(args[2], "%d", &n);
    
    if(address < 0x2000000 || address > 0x3007fff) {
      printf("Invalid address: %08x\n", address);
      return;
    }
    
    if(address > 0x203ffff && address < 0x3000000) {
      printf("Invalid address: %08x\n", address);
      return;
    }
    
    u32 final = address + n;    
    switch(address >> 24) {
    case 2:
      {
        address &= 0x3ffff;
        final &= 0x3ffff;
        for(int i = address; i < final; i++)
          if(freezeWorkRAM[i] == 1)
            freezeWorkRAM[i] = 0;
        printf("Cleared break on write from %08x to %08x\n",
               0x2000000+address, 0x2000000+final);
      }
      break;
    case 3:
      {
        address &= 0x7fff;
        final &= 0x7fff;
        for(int i = address; i < final; i++)
          if(freezeInternalRAM[i] == 1)
            freezeInternalRAM[i] = 0;
        printf("Cleared break on write from %08x to %08x\n",
               0x3000000+address, 0x3000000+final);
      }
      break;
    }
  } else if(n == 1) {
    int i;
    for(i = 0; i < 0x40000; i++)
      if(freezeWorkRAM[i] == 1)
        freezeWorkRAM[i] = 0;
    for(i = 0; i < 0x8000; i++)
      if(freezeInternalRAM[i] == 1)
        freezeInternalRAM[i] = 0;

    printf("Cleared all break on write\n");
  } else
    debuggerUsage("bpwc");
}

static void debuggerBreakWrite(int n, char **args)
{
  if(n == 3) {
    if(cheatsNumber != 0) {
      printf("Cheats are enabled. Cannot continue.\n");
      return;
    }
    u32 address = 0;
    sscanf(args[1], "%x", &address);
    int n = 0;
    sscanf(args[2], "%d", &n);
    
    if(address < 0x2000000 || address > 0x3007fff) {
      printf("Invalid address: %08x\n", address);
      return;
    }
    
    if(address > 0x203ffff && address < 0x3000000) {
      printf("Invalid address: %08x\n", address);
      return;
    }

    u32 final = address + n;

    if(address < 0x2040000 && final > 0x2040000) {
      printf("Invalid byte count: %d\n", n);
      return;
    } else if(address < 0x3008000 && final > 0x3008000) {
      printf("Invalid byte count: %d\n", n);
      return;
    }
    printf("Added break on write at %08x for %d bytes\n", address, n);
    for(int i = 0; i < n; i++) {
      if((address >> 24) == 2)
        freezeWorkRAM[address & 0x3ffff] = 1;
      else
        freezeInternalRAM[address & 0x7fff] = 1;
      address++;
    }
  } else
    debuggerUsage("bpw");    
}

static void debuggerBreakChangeClear(int n, char **args)
{
  if(n == 3) {
    u32 address = 0;
    sscanf(args[1], "%x", &address);
    int n = 0;
    sscanf(args[2], "%d", &n);
    
    if(address < 0x2000000 || address > 0x3007fff) {
      printf("Invalid address: %08x\n", address);
      return;
    }
    
    if(address > 0x203ffff && address < 0x3000000) {
      printf("Invalid address: %08x\n", address);
      return;
    }
    
    u32 final = address + n;    
    switch(address >> 24) {
    case 2:
      {
        address &= 0x3ffff;
        final &= 0x3ffff;
        for(int i = address; i < final; i++)
          if(freezeWorkRAM[i] == 2)
            freezeWorkRAM[i] = 0;
        printf("Cleared break on change from %08x to %08x\n",
               0x2000000+address, 0x2000000+final);
      }
      break;
    case 3:
      {
        address &= 0x7fff;
        final &= 0x7fff;
        for(int i = address; i < final; i++)
          if(freezeInternalRAM[i] == 2)
            freezeInternalRAM[i] = 0;
        printf("Cleared break on change from %08x to %08x\n",
               0x3000000+address, 0x3000000+final);
      }
      break;
    }
  } else if(n == 1) {
    int i;
    for(i = 0; i < 0x40000; i++)
      if(freezeWorkRAM[i] == 2)
        freezeWorkRAM[i] = 0;
    for(i = 0; i < 0x8000; i++)
      if(freezeInternalRAM[i] == 2)
        freezeInternalRAM[i] = 0;
    
    printf("Cleared all break on change\n");
  } else
    debuggerUsage("bpcc");
}

static void debuggerBreakChange(int n, char **args)
{
  if(n == 3) {
    if(cheatsNumber != 0) {
      printf("Cheats are enabled. Cannot continue.\n");
      return;
    }
    u32 address = 0;
    sscanf(args[1], "%x", &address);
    int n = 0;
    sscanf(args[2], "%d", &n);
    
    if(address < 0x2000000 || address > 0x3007fff) {
      printf("Invalid address: %08x\n", address);
      return;
    }
    
    if(address > 0x203ffff && address < 0x3000000) {
      printf("Invalid address: %08x\n", address);
      return;
    }

    u32 final = address + n;

    if(address < 0x2040000 && final > 0x2040000) {
      printf("Invalid byte count: %d\n", n);
      return;
    } else if(address < 0x3008000 && final > 0x3008000) {
      printf("Invalid byte count: %d\n", n);
      return;
    }
    printf("Added break on change at %08x for %d bytes\n", address, n);
    for(int i = 0; i < n; i++) {
      if((address >> 24) == 2)
        freezeWorkRAM[address & 0x3ffff] = 2;
      else
        freezeInternalRAM[address & 0x7fff] = 2;
      address++;
    }
  } else
    debuggerUsage("bpc");
}

static void debuggerDisassembleArm(FILE *f, u32 pc, int count)
{
  char buffer[80];
  int i = 0;
  int len = 0;
  char format[30];
  for(i = 0; i < count; i++) {
    size_t l = strlen(elfGetAddressSymbol(pc+4*i));
    if(l > len)
      len = l;
  }
  sprintf(format, "%%08x %%-%ds %%s\n", len);
  for(i = 0; i < count; i++) {
    u32 addr = pc;
    pc += disArm(pc, buffer, 2);
    fprintf(f, format, addr, elfGetAddressSymbol(addr), buffer);
  }
}

static void debuggerDisassembleThumb(FILE *f, u32 pc, int count)
{
  char buffer[80];
  int i = 0;
  int len = 0;
  char format[30];
  for(i = 0; i < count; i++) {
    size_t l = strlen(elfGetAddressSymbol(pc+2*i));
    if(l > len)
      len = l;
  }
  sprintf(format, "%%08x %%-%ds %%s\n", len);  
  
  for(i = 0; i < count; i++) {
    u32 addr = pc;
    pc += disThumb(pc, buffer, 2);
    fprintf(f, format, addr, elfGetAddressSymbol(addr), buffer);
  }
}

static void debuggerDisassembleArm(int n, char **args)
{
  u32 pc = reg[15].I;
  pc -= 4;
  int count = 20;
  if(n >= 2) {
    sscanf(args[1], "%x", &pc);
  }
  if(pc & 3) {
    printf("Misaligned address %08x\n", pc);
    pc &= 0xfffffffc;
  }
  if(n >= 3) {
    sscanf(args[2], "%d", &count);
  }
  debuggerDisassembleArm(stdout, pc, count); 
}

static void debuggerDisassembleThumb(int n, char **args)
{
  u32 pc = reg[15].I;
  pc -= 2;
  int count = 20;
  if(n >= 2) {
    sscanf(args[1], "%x", &pc);
  }
  if(pc & 1) {
    printf("Misaligned address %08x\n", pc);
    pc &= 0xfffffffe;
  }
  if(n >= 3) {
    sscanf(args[2], "%d", &count);
  }
  debuggerDisassembleThumb(stdout, pc, count);
}

static void debuggerDisassemble(int n, char **args)
{
  if(armState)
    debuggerDisassembleArm(n, args);
  else
    debuggerDisassembleThumb(n, args);
}

static void debuggerFileDisassembleArm(int n, char **args)
{
  u32 pc = reg[15].I;
  pc -= 4;
  int count = 20;
  if(n < 2) {
    debuggerUsage("fda");
    return;
  }
  FILE *f = fopen(args[1], "w+");
  if(!f) {
    printf("Error: cannot open file %s\n", args[1]);
    return;
  }
  if(n >= 3) {
    sscanf(args[2], "%x", &pc);
  }
  if(pc & 3) {
    printf("Misaligned address %08x\n", pc);
    pc &= 0xfffffffc;
  }
  if(n >= 4) {
    sscanf(args[3], "%d", &count);
  }
  debuggerDisassembleArm(f, pc, count); 
  fclose(f);
}

static void debuggerFileDisassembleThumb(int n, char **args)
{
  u32 pc = reg[15].I;
  pc -= 2;
  int count = 20;
  if(n < 2) {
    debuggerUsage("fdt");
    return;
  }
  FILE *f = fopen(args[1], "w+");
  if(!f) {
    printf("Error: cannot open file %s\n", args[1]);
    return;
  }
    
  if(n >= 3) {
    sscanf(args[2], "%x", &pc);
  }
  if(pc & 1) {
    printf("Misaligned address %08x\n", pc);
    pc &= 0xfffffffe;
  }
  if(n >= 4) {
    sscanf(args[3], "%d", &count);
  }
  debuggerDisassembleThumb(f, pc, count);
  fclose(f);
}

static void debuggerFileDisassemble(int n, char **args)
{
  if(n < 2) {
    debuggerUsage("fd");
  } else {
    if(armState)
      debuggerFileDisassembleArm(n, args);
    else
      debuggerFileDisassembleThumb(n, args);
  }
}

static void debuggerContinueAfterBreakpoint()
{
  printf("Continuing after breakpoint\n");
  debuggerEnableBreakpoints(true);
  debuggerPrefetch();
  emulator.emuMain(1);
  debuggerAtBreakpoint = false;
}

static void debuggerRegisters(int, char **)
{
  char *command[3];
  char buffer[10];

  printf("R00=%08x R04=%08x R08=%08x R12=%08x\n",
         reg[0].I, reg[4].I, reg[8].I, reg[12].I);
  printf("R01=%08x R05=%08x R09=%08x R13=%08x\n",
         reg[1].I, reg[5].I, reg[9].I, reg[13].I);
  printf("R02=%08x R06=%08x R10=%08x R14=%08x\n",
         reg[2].I, reg[6].I, reg[10].I, reg[14].I);
  printf("R03=%08x R07=%08x R11=%08x R15=%08x\n",
         reg[3].I, reg[7].I, reg[11].I, reg[15].I);
  printf("CPSR=%08x (%c%c%c%c%c%c%c Mode: %02x)\n",
         reg[16].I,
         (N_FLAG ? 'N' : '.'),
         (Z_FLAG ? 'Z' : '.'),
         (C_FLAG ? 'C' : '.'),
         (V_FLAG ? 'V' : '.'),
         (armIrqEnable ? '.' : 'I'),
         ((!(reg[16].I & 0x40)) ? '.' : 'F'),
         (armState ? '.' : 'T'),
         armMode);
  sprintf(buffer,"%08x", armState ? reg[15].I - 4 : reg[15].I - 2);
  command[0]="m";
  command[1]=buffer;
  command[2]="1";
  debuggerDisassemble(3, command);
}

static void debuggerIoVideo()
{
  printf("DISPCNT  = %04x\n", DISPCNT);
  printf("DISPSTAT = %04x\n", DISPSTAT);
  printf("VCOUNT   = %04x\n", VCOUNT);
  printf("BG0CNT   = %04x\n", BG0CNT);
  printf("BG1CNT   = %04x\n", BG1CNT);
  printf("BG2CNT   = %04x\n", BG2CNT);
  printf("BG3CNT   = %04x\n", BG3CNT);
  printf("WIN0H    = %04x\n", WIN0H);
  printf("WIN0V    = %04x\n", WIN0V);
  printf("WIN1H    = %04x\n", WIN1H);
  printf("WIN1V    = %04x\n", WIN1V);
  printf("WININ    = %04x\n", WININ);
  printf("WINOUT   = %04x\n", WINOUT);
  printf("MOSAIC   = %04x\n", MOSAIC);
  printf("BLDMOD   = %04x\n", BLDMOD);
  printf("COLEV    = %04x\n", COLEV);
  printf("COLY     = %04x\n", COLY);
}

static void debuggerIoVideo2()
{
  printf("BG0HOFS  = %04x\n", BG0HOFS);
  printf("BG0VOFS  = %04x\n", BG0VOFS);
  printf("BG1HOFS  = %04x\n", BG1HOFS);
  printf("BG1VOFS  = %04x\n", BG1VOFS);
  printf("BG2HOFS  = %04x\n", BG2HOFS);
  printf("BG2VOFS  = %04x\n", BG2VOFS);
  printf("BG3HOFS  = %04x\n", BG3HOFS);
  printf("BG3VOFS  = %04x\n", BG3VOFS);
  printf("BG2PA    = %04x\n", BG2PA);
  printf("BG2PB    = %04x\n", BG2PB);
  printf("BG2PC    = %04x\n", BG2PC);
  printf("BG2PD    = %04x\n", BG2PD);
  printf("BG2X     = %08x\n", (BG2X_H<<16)|BG2X_L);
  printf("BG2Y     = %08x\n", (BG2Y_H<<16)|BG2Y_L);
  printf("BG3PA    = %04x\n", BG3PA);
  printf("BG3PB    = %04x\n", BG3PB);
  printf("BG3PC    = %04x\n", BG3PC);
  printf("BG3PD    = %04x\n", BG3PD);
  printf("BG3X     = %08x\n", (BG3X_H<<16)|BG3X_L);
  printf("BG3Y     = %08x\n", (BG3Y_H<<16)|BG3Y_L);
}

static void debuggerIoDMA()
{
  printf("DM0SAD   = %08x\n", (DM0SAD_H<<16)|DM0SAD_L);
  printf("DM0DAD   = %08x\n", (DM0DAD_H<<16)|DM0DAD_L);
  printf("DM0CNT   = %08x\n", (DM0CNT_H<<16)|DM0CNT_L);  
  printf("DM1SAD   = %08x\n", (DM1SAD_H<<16)|DM1SAD_L);
  printf("DM1DAD   = %08x\n", (DM1DAD_H<<16)|DM1DAD_L);
  printf("DM1CNT   = %08x\n", (DM1CNT_H<<16)|DM1CNT_L);  
  printf("DM2SAD   = %08x\n", (DM2SAD_H<<16)|DM2SAD_L);
  printf("DM2DAD   = %08x\n", (DM2DAD_H<<16)|DM2DAD_L);
  printf("DM2CNT   = %08x\n", (DM2CNT_H<<16)|DM2CNT_L);  
  printf("DM3SAD   = %08x\n", (DM3SAD_H<<16)|DM3SAD_L);
  printf("DM3DAD   = %08x\n", (DM3DAD_H<<16)|DM3DAD_L);
  printf("DM3CNT   = %08x\n", (DM3CNT_H<<16)|DM3CNT_L);    
}

static void debuggerIoTimer()
{
  printf("TM0D     = %04x\n", TM0D);
  printf("TM0CNT   = %04x\n", TM0CNT);
  printf("TM1D     = %04x\n", TM1D);
  printf("TM1CNT   = %04x\n", TM1CNT);
  printf("TM2D     = %04x\n", TM2D);
  printf("TM2CNT   = %04x\n", TM2CNT);
  printf("TM3D     = %04x\n", TM3D);
  printf("TM3CNT   = %04x\n", TM3CNT);
}

static void debuggerIoMisc()
{
  printf("P1       = %04x\n", P1);  
  printf("IE       = %04x\n", IE);
  printf("IF       = %04x\n", IF);
  printf("IME      = %04x\n", IME);
}

static void debuggerIo(int n, char **args)
{
  if(n == 1) {
    debuggerIoVideo();
    return;
  }
  if(!strcmp(args[1], "video"))
    debuggerIoVideo();
  else if(!strcmp(args[1], "video2"))
    debuggerIoVideo2();
  else if(!strcmp(args[1], "dma"))
    debuggerIoDMA();
  else if(!strcmp(args[1], "timer"))
    debuggerIoTimer();
  else if(!strcmp(args[1], "misc"))
    debuggerIoMisc();
  else printf("Unrecognized option %s\n", args[1]);
}

static void debuggerEditByte(int n, char **args)
{
  if(n == 3) {
    u32 address = 0x10000;
    u32 byte = 0;
    sscanf(args[1], "%x", &address);
    sscanf(args[2], "%x", &byte);
    debuggerWriteByte(address, (u8)byte);
  } else
    debuggerUsage("eb");    
}

static void debuggerEditHalfWord(int n, char **args)
{
  if(n == 3) {
    u32 address = 0x10000;
    u32 HalfWord = 0;
    sscanf(args[1], "%x", &address);
    if(address & 1) {
      printf("Error: address must be half-word aligned\n");
      return;
    }
    sscanf(args[2], "%x", &HalfWord);
    debuggerWriteHalfWord(address, (u16)HalfWord);
  } else
    debuggerUsage("eh");        
}

static void debuggerEditRegister(int n, char **args)
{
  if(n == 3) {
    int r = 15;
    u32 val;
    sscanf(args[1], "%d", &r);
    if(r > 16 || r == 15) {
      // don't allow PC to change
      printf("Error: Register must be valid (0-14,16)\n");
      return;
    }
    sscanf(args[2], "%x", &val);
    reg[r].I=val;
    printf("Register changed.\n");
  } else
    debuggerUsage("er");    
}

static void debuggerEdit(int n, char **args)
{
  if(n == 3) {
    u32 address;
    u32 byte;
    sscanf(args[1], "%x", &address);
    if(address & 3) {
      printf("Error: address must be word aligned\n");
      return;
    }
    sscanf(args[2], "%x", &byte);
    debuggerWriteMemory(address, (u32)byte);
  } else
    debuggerUsage("ew");    
}


#define ASCII(c) (c) < 32 ? '.' : (c) > 127 ? '.' : (c)

static void debuggerMemoryByte(int n, char **args)
{
  if(n == 2) {
    u32 addr = 0;
    sscanf(args[1], "%x", &addr);
    for(int _i = 0; _i < 16; _i++) {
      int a = debuggerReadByte(addr);
      int b = debuggerReadByte(addr+1);
      int c = debuggerReadByte(addr+2);
      int d = debuggerReadByte(addr+3);
      int e = debuggerReadByte(addr+4);
      int f = debuggerReadByte(addr+5);
      int g = debuggerReadByte(addr+6);
      int h = debuggerReadByte(addr+7);
      int i = debuggerReadByte(addr+8);
      int j = debuggerReadByte(addr+9);
      int k = debuggerReadByte(addr+10);
      int l = debuggerReadByte(addr+11);
      int m = debuggerReadByte(addr+12);
      int n = debuggerReadByte(addr+13);
      int o = debuggerReadByte(addr+14);
      int p = debuggerReadByte(addr+15);
      
      printf("%08x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c\n",
             addr,a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,
             ASCII(a),ASCII(b),ASCII(c),ASCII(d),
             ASCII(e),ASCII(f),ASCII(g),ASCII(h),
             ASCII(i),ASCII(j),ASCII(k),ASCII(l),
             ASCII(m),ASCII(n),ASCII(o),ASCII(p));
      addr += 16;
    }
  } else
    debuggerUsage("mb");    
}

static void debuggerMemoryHalfWord(int n, char **args)
{
  if(n == 2) {
    u32 addr = 0;
    sscanf(args[1], "%x", &addr);
    addr = addr & 0xfffffffe;
    for(int _i = 0; _i < 16; _i++) {
      int a = debuggerReadByte(addr);
      int b = debuggerReadByte(addr+1);
      int c = debuggerReadByte(addr+2);
      int d = debuggerReadByte(addr+3);
      int e = debuggerReadByte(addr+4);
      int f = debuggerReadByte(addr+5);
      int g = debuggerReadByte(addr+6);
      int h = debuggerReadByte(addr+7);
      int i = debuggerReadByte(addr+8);
      int j = debuggerReadByte(addr+9);
      int k = debuggerReadByte(addr+10);
      int l = debuggerReadByte(addr+11);
      int m = debuggerReadByte(addr+12);
      int n = debuggerReadByte(addr+13);
      int o = debuggerReadByte(addr+14);
      int p = debuggerReadByte(addr+15);
      
      printf("%08x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c\n",
             addr,b,a,d,c,f,e,h,g,j,i,l,k,n,m,p,o,
             ASCII(a),ASCII(b),ASCII(c),ASCII(d),
             ASCII(e),ASCII(f),ASCII(g),ASCII(h),
             ASCII(i),ASCII(j),ASCII(k),ASCII(l),
             ASCII(m),ASCII(n),ASCII(o),ASCII(p));
      addr += 16;
    }
  } else
    debuggerUsage("mh");    
}

static void debuggerMemory(int n, char **args)
{
  if(n == 2) {
    u32 addr = 0;
    sscanf(args[1], "%x", &addr);
    addr = addr & 0xfffffffc;
    for(int _i = 0; _i < 16; _i++) {
      int a = debuggerReadByte(addr);
      int b = debuggerReadByte(addr+1);
      int c = debuggerReadByte(addr+2);
      int d = debuggerReadByte(addr+3);

      int e = debuggerReadByte(addr+4);
      int f = debuggerReadByte(addr+5);
      int g = debuggerReadByte(addr+6);
      int h = debuggerReadByte(addr+7);

      int i = debuggerReadByte(addr+8);
      int j = debuggerReadByte(addr+9);
      int k = debuggerReadByte(addr+10);
      int l = debuggerReadByte(addr+11);

      int m = debuggerReadByte(addr+12);
      int n = debuggerReadByte(addr+13);
      int o = debuggerReadByte(addr+14);
      int p = debuggerReadByte(addr+15);
      
      printf("%08x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c\n",
             addr,d,c,b,a,h,g,f,e,l,k,j,i,p,o,n,m,
             ASCII(a),ASCII(b),ASCII(c),ASCII(d),
             ASCII(e),ASCII(f),ASCII(g),ASCII(h),
             ASCII(i),ASCII(j),ASCII(k),ASCII(l),
             ASCII(m),ASCII(n),ASCII(o),ASCII(p));
      addr += 16;
    }
  } else
    debuggerUsage("mw");    
}

static void debuggerQuit(int, char **)
{
  char buffer[10];
  printf("Are you sure you want to quit (y/n)? ");
  fgets(buffer, 1024, stdin);
  
  if(buffer[0] == 'y' || buffer[0] == 'Y') {
    debugger = false;
    emulating = false;
  }
}

static void debuggerWriteState(int n, char **args)
{
  int num = 12;

  if(n == 2) {
    sscanf(args[1],"%d",&num);
    if(num > 0 && num < 11)
      sdlWriteState(num-1);
    else
      printf("Savestate number must be in the 1-10 range");
  }
  else
    debuggerUsage("save");
}

static void debuggerReadState(int n, char **args)
{
  int num = 12;

  if(n == 2) {
    sscanf(args[1],"%d",&num);
    if(num > 0 && num < 11)
      sdlReadState(num-1);
    else
      printf("Savestate number must be in the 1-10 range");
  }
  else
    debuggerUsage("load");
}

static void debuggerDumpLoad(int n, char** args)
{
  u32 address = 0;
  char *file;
  FILE *f;
  int c;
  
  if(n==3) {
    file=args[1];
    
    sscanf(args[2],"%x",&address);
    
    f=fopen(file,"rb");
    if(!f) {
      printf("Error opening file.\n");
      return;
    }
    
    fseek(f,0,SEEK_END);
    int size=ftell(f);
    fseek(f,0,SEEK_SET);
    
    for(int i=0;i<size;i++) {
      c = fgetc(f);
      if(c == -1)
        break;
      debuggerWriteByte(address,c);
      address++;
    }

    fclose(f);
  } else
    debuggerUsage("dload");
}

static void debuggerDumpSave(int n, char** args)
{
  u32 address = 0;
  u32 size = 0;
  char *file;
  FILE *f;
  
  if(n==4) {
    file=args[1];
    sscanf(args[2],"%x",&address);
    sscanf(args[3],"%x",&size);
    
    f=fopen(file,"wb");
    if(!f) {
      printf("Error opening file.\n");
      return;
    }
    
    for(u32 i = 0; i < size; i++) {
      fputc(debuggerReadByte(address), f);
      address++;
    }
    
    fclose(f);
  } else
    debuggerUsage("dsave");
}

static void debuggerCondBreakThumb(int n, char **args)
{
  if(n > 4) { //conditional args handled separately
    int i = debuggerNumOfBreakpoints;
    
    u32 address = 0;
    sscanf(args[1],"%x", &address);
    
    debuggerBreakpointList[i].address = address;
    debuggerBreakpointList[i].value = debuggerReadHalfWord(address);
    debuggerBreakpointList[i].size = 0;
    
    debuggerCondValidate(n, args,2);
  } else
    debuggerUsage("cbt");

}

static void debuggerCondBreakArm(int n, char **args)
{
  if(n > 4) { //conditional args handled separately
    
    int i = debuggerNumOfBreakpoints;
    u32 address = 0;
    
    sscanf(args[1],"%x", &address);
    debuggerBreakpointList[i].address = address;
    debuggerBreakpointList[i].value = debuggerReadMemory(address);
    debuggerBreakpointList[i].size = 1;
    debuggerCondValidate(n, args,2);
  } else
    debuggerUsage("cba");
}

static void debuggerCondValidate(int n,char **args,int start)
{
  /*
    0: address/register
    1: op
    2: value
    3: size
  */
  
  int i=debuggerNumOfBreakpoints;
  
  char *address=args[start];
  char *op=args[start+1];
  char *value=args[start+2];
  char *returnmsg="";
  char *tsize,*taddress,*tvalue;

  int rel=0;
  
  u32 value1=0;
  u32 value2=0;
  
  char size=0;
  int j=1;
  
  if(n==6) {
    size = args[start+3][0];
    if(size != 'b' && size != 'h' && size != 'w') {
      printf("Invalid size.\n");
      return;
    }
    
    switch(size) {
    case 'b':
      debuggerBreakpointList[i].cond_size=1;
      tsize="byte";
      break;
    case 'h':
      debuggerBreakpointList[i].cond_size=2;
      tsize="halfword";
      break;
    case 'w':
      debuggerBreakpointList[i].cond_size=4;
      tsize="word";
      break;
    }
  }

  switch(toupper(address[0])) {
  case '$': //is address
    while (address[j]) {
      address[j-1]=address[j];
      j++;
    }
    address[j-1]=0;
    
    sscanf(address,"%x",&value1);
    switch(size) {
    case 'h':
      if(value1 & 1) {
        printf("Misaligned Conditional Address.\n");
        return;
      }
      break;
    case 'w':
      if(value1 & 3) {
        printf("Misaligned Conditional Address.\n");
        return;
      }
    case 'b':
      break;
    default:
      printf("Erroneous Condition\n");
      debuggerUsage((char *)((toupper(args[0][2])=='T') ? "cbt" : "cba"));
      return;
    }
    debuggerBreakpointList[i].ia1=true;
    taddress="$";
    break;
  case 'R': //is register
    while(address[j]) {
      address[j-1]=address[j];
      j++;
    }
    address[j-1]=0;
    sscanf(address,"%d",&value1);
    
    if(value1 > 16) {
      printf("Invalid Register.\n");
      return;
    }
    if(size)
      size=0;
    debuggerBreakpointList[i].ia1=true;
    taddress="r";
    break;
  default: //immediate;
    printf("First Comparison Parameter should not be Immediate\n");
    return;
  }

  debuggerBreakpointList[i].cond_address = value1;

  // Check op 
  switch(op[0]) {
  case '=': // 1
    if (op[1] == '=' && op[2]==0)
      rel=1;
    else 
      goto error;
    break;
  case '!': //2
    if (op[1]=='=' && op[2]==0)
      rel=2;
    else
      goto error;
    break;
  case '<': //3
    if(op[1]=='=')
      rel=5;
    else if (op[1]==0)
      rel=3;
    else
      goto error;
    break;
  case '>': //4
    if (op[1]=='=')
      rel=6;
    else if (op[1]==0)
      rel=4;
    else
      goto error;
    break;
  default:
  error:
    printf("Invalid comparison operator.\n");
    return;
  }
  
  if(op==0) {
    printf("Invalid comparison operator.\n");
    return;
  }
  debuggerBreakpointList[i].cond_rel=rel;

  switch(toupper(value[0])) {
  case '$': //is address
    while(value[j]) {
      value[j-1]=value[j];
      j++;
    }
    value[j-1]=0;

    sscanf(value,"%x",&value2);
    debuggerBreakpointList[i].ia2=true;
    tvalue="$";
    switch(size) {
    case 'h':
      if(value2 & 1) {
        printf("Misaligned Conditional Address.\n");
        return;
      }
      break;
    case 'w':
      if(value2 & 3) {
        printf("Misaligned Conditional Address.\n");
        return;
      }
    case 'b':
      break;
    default:
      printf("Erroneous Condition\n");
      debuggerUsage((char *)((toupper(args[0][2])=='T') ? "cbt" : "cba"));
      return;
    }
    break;
  case 'R': //is register
    while(value[j]) {
      value[j-1]=value[j];
      j++;
    }
    value[j-1]=0;
    sscanf(value,"%d",&value2);
    
    if(value2 > 16) {
      printf("Invalid Register.\n");
      return;
    }
    debuggerBreakpointList[i].ia2=true;
    tvalue="r";
    break;
  default: //immediate;
    sscanf(value, "%x",&value2);
    debuggerBreakpointList[i].ia2=false;
    tvalue="0x";
    
    switch(size) {
    case 'b':
      value2 &=0xFF;
      break;
    case 'h':
      value2 &=0xFFFF;
      break;
    default:
    case 'w':
      value2 &=0xFFFFFFFF;
      break;
    }
    break;
  }
  
  debuggerBreakpointList[i].cond_value = value2;
  debuggerNumOfBreakpoints++;
  
  // At here, everything's set. Display message.
  switch(size) {
  case 0:
    printf("Added breakpoint on %08X if R%02d %s %08X\n",
           debuggerBreakpointList[i].address,
           debuggerBreakpointList[i].cond_address,
           op,
           debuggerBreakpointList[i].cond_value);
    break;
  case 'b':
    printf("Added breakpoint on %08X if %s%08X %s %s%02X\n",
           debuggerBreakpointList[i].address,
           taddress,
           debuggerBreakpointList[i].cond_address,
           op,tvalue,
           debuggerBreakpointList[i].cond_value);
    break;
  case 'h':
    printf("Added breakpoint on %08X if %s%08X %s %s%04X\n",
           debuggerBreakpointList[i].address,
           taddress,
           debuggerBreakpointList[i].cond_address,
           op,
           tvalue,
           debuggerBreakpointList[i].cond_value);
    break;
  case 'w':
    printf("Added breakpoint on %08X if %s%08X %s %s%08X\n",
           debuggerBreakpointList[i].address,
           taddress,
           debuggerBreakpointList[i].cond_address,
           op,tvalue,
           debuggerBreakpointList[i].cond_value);
    break;
  }
}

static bool debuggerCondEvaluate(int num)
{
  // check if there is a condition
  if(debuggerBreakpointList[num].cond_rel == 0)
    return true;
  
  u32 address=debuggerBreakpointList[num].cond_address;
  char size=debuggerBreakpointList[num].cond_size;
  u32 value=debuggerBreakpointList[num].cond_value;
  u32 value1=0;
  u32 value2=0;
  
  if(address<17)
    value1=reg[address].I;
  else {
    switch(size) {
    case 1:
      value1=debuggerReadByte(address);
      break;
    case 2:
      value1=debuggerReadHalfWord(address);
      break;
    default:
      value1=debuggerReadMemory(address);
      break;
    }
  }
	
  //value2
  if(debuggerBreakpointList[num].ia2) { //is address or register
    if(value<17)
      value2=reg[address].I;
    else {
      switch(size) {
      case 'b':
        value2=debuggerReadByte(value);
        break;
      case 'h':
        value2=debuggerReadHalfWord(value);
        break;
      default:
        value2=debuggerReadMemory(value);
        break;
      }
    }
  } else
    value2=value;

  switch(debuggerBreakpointList[num].cond_rel) {
  case 1: // ==
    return (value1 == value2);
  case 2: // !=
    return (value1 != value2);
  case 3: // <
    return (value1 < value2);
  case 4: // >
    return (value1 > value2);
  case 5: // <=
    return (value1 <= value2);
  case 6: // >=
    return (value1 >= value2);
  default:
    return false; //should never happen
  }
}

/*extern*/ void debuggerOutput(char *s, u32 addr)
{
  if(s)
    printf(s);
  else {
    char c;

    c = debuggerReadByte(addr);
    addr++;
    while(c) {
      putchar(c);
      c = debuggerReadByte(addr);
      addr++;
    }
  }
}

/*extern*/ void debuggerMain()
{
  char buffer[1024];
  char *commands[10];
  int commandCount = 0;
  
  if(emulator.emuUpdateCPSR)
    emulator.emuUpdateCPSR();
  debuggerRegisters(0, NULL);
  
  while(debugger) {
    systemSoundPause();
    debuggerDisableBreakpoints();
    printf("debugger> ");
    commandCount = 0;
    char *s = fgets(buffer, 1024, stdin);

    commands[0] = strtok(s, " \t\n");
    if(commands[0] == NULL)
      continue;
    commandCount++;
    while((s = strtok(NULL, " \t\n"))) {
      commands[commandCount++] = s;
      if(commandCount == 10)
        break;
    }

    for(int j = 0; ; j++) {
      if(debuggerCommands[j].name == NULL) {
        printf("Unrecognized command %s. Type h for help.\n", commands[0]);
        break;
      }
      if(!strcmp(commands[0], debuggerCommands[j].name)) {
        debuggerCommands[j].function(commandCount, commands);
        break;
      }
    } 
  }
}
