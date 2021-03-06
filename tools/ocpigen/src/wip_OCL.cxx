/*
 *  Copyright (c) Mercury Federal Systems, Inc., Arlington VA., 2009-2011
 *
 *    Mercury Federal Systems, Incorporated
 *    1901 South Bell Street
 *    Suite 402
 *    Arlington, Virginia 22202
 *    United States of America
 *    Telephone 703-413-0781
 *    FAX 703-413-0784
 *
 *  This file is part of OpenCPI (www.opencpi.org).
 *     ____                   __________   ____
 *    / __ \____  ___  ____  / ____/ __ \ /  _/ ____  _________ _
 *   / / / / __ \/ _ \/ __ \/ /   / /_/ / / /  / __ \/ ___/ __ `/
 *  / /_/ / /_/ /  __/ / / / /___/ ____/_/ / _/ /_/ / /  / /_/ /
 *  \____/ .___/\___/_/ /_/\____/_/    /___/(_)____/_/   \__, /
 *      /_/                                             /____/
 *
 *  OpenCPI is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published
 *  by the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  OpenCPI is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with OpenCPI.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <assert.h>
#include <ctype.h>
#include <sys/time.h>
#include "OcpiUtilCppMacros.h"
#include "OcpiUtilMisc.h"
#include "wip.h"

namespace OU = OCPI::Util;
// Generate the readonly implementation file.
// What implementations must explicitly (verilog) or implicitly (VHDL) include.
#define HEADER ".h"
#define OCLIMPL "_Worker"
#define SOURCE ".cl"
#define OCLENTRYPOINT "_entry_point"

static const char *
upperdup(const char *s) {
  char *upper = (char *)malloc(strlen(s) + 1);
  for (char *u = upper; (*u++ = (char)toupper(*s)); s++)
    ;
  return upper;
}

static const char* emitEntryPointOCL ( Worker* w,
                                       const char* outDir );

static const char *oclTypes[] = {"none",
  "OCLBoolean", "OCLChar", "OCLDouble", "OCLFloat", "int16_t", "int32_t", "uint8_t",
  "uint32_t", "uint16_t", "int64_t", "uint64_t", "OCLChar" };

static void
printMember(FILE *f, OU::Member *t, const char *prefix, size_t &offset, unsigned &pad)
{
  size_t rem = offset & (t->m_align - 1);
  if (rem)
    fprintf(f, "%s  char         pad%u_[%zu];\n",
	    prefix, pad++, t->m_align - rem);
  offset = OU::roundUp(offset, t->m_align);
  if (t->m_isSequence) {
    fprintf(f, "%s  uint32_t     %s_length;\n", prefix, t->m_name.c_str());
    if (t->m_align > sizeof(uint32_t))
      fprintf(f, "%s  char         pad%u_[%zu];\n",
	      prefix, pad++, t->m_align - (unsigned)sizeof(uint32_t));
    offset += t->m_align;
  }
  fprintf(f, "%s  %-12s %s", prefix, oclTypes[t->m_baseType], t->m_name.c_str());
  if (t->m_baseType == OA::OCPI_String)
    fprintf(f, "[%zu]", OU::roundUp(t->m_stringLength + 1, 4));
  if (t->m_isSequence || t->m_arrayRank)
    fprintf(f, "[%zu]", t->m_sequenceLength);
  fprintf(f, "; // offset %zu, 0x%zx\n", offset, offset);
  offset += t->m_nBytes;
}

static const char *
methodName(Worker *w, const char *method, const char *&mName) {
  const char *pat = w->m_pattern ? w->m_pattern : w->m_staticPattern;
  if (!pat) {
    mName = method;
    return 0;
  }
  size_t length =
    strlen(w->m_implName) + strlen(method) + strlen(pat) + 1;
  char c, *s = (char *)malloc(length);
  mName = s;
  while ((c = *pat++)) {
    if (c != '%')
      *s++ = c;
    else if (!*pat)
      *s++ = '%';
    else switch (*pat++) {
      case '%':
	*s++ = '%';
	break;
      case 'm':
	strcpy(s, method);
	while (*s)
	  s++;
	break;
      case 'M':
	*s++ = (char)toupper(method[0]);
	strcpy(s, method + 1);
	while (*s)
	  s++;
	break;
      case 'w':
	strcpy(s, w->m_implName);
	while (*s)
	  s++;
	break;
      case 'W':
	*s++ = (char)toupper(w->m_implName[0]);
	strcpy(s, w->m_implName + 1);
	while (*s)
	  s++;
	break;
      default:
	return OU::esprintf("Invalid pattern rule: %s", w->m_pattern);
      }
  }
  *s++ = 0;
  return 0;
}

/*
  FIXME
  1. add support for local memory.
     a. Get local memory sizes from metadata
     b. Add local memory structures to the worker context

*/
static void
emitStructOCL(FILE *f, size_t nMembers, OU::Member *members, const char *indent) {
  size_t align = 0;
  unsigned pad = 0;
  for (unsigned n = 0; n < nMembers; n++, members++)
    printMember(f, members, indent, align, pad);
}

const char *
emitImplOCL(Worker *w, const char *outDir) {
  const char *err;
  FILE *f;
  if ((err = openOutput(w->m_fileName.c_str(), outDir, "", OCLIMPL, HEADER, w->m_implName, f)))
    return err;
  fprintf(f, "/*\n");
  printgen(f, " *", w->m_file.c_str());
  fprintf(f, " */\n");
  const char *upper = upperdup(w->m_implName);
  fprintf(f,
          "\n"
          "/* This file contains the implementation declarations for worker %s */\n\n"
          "#ifndef OCL_WORKER_%s_H__\n"
          "#define OCL_WORKER_%s_H__\n\n"
          "#include <OCL_Worker.h>\n\n"
          "#if defined (__cplusplus)\n"
          "extern \"C\" {\n"
          "#endif\n\n",
          w->m_implName, upper, upper);

  if (w->m_ctl.properties.size()) {
    fprintf(f,
            "/*\n"
            " * Property structure for worker %s\n"
            " */\n"
            "typedef struct {\n",
            w->m_implName);
    size_t align = 0;
    unsigned pad = 0;
    for (PropertiesIter pi = w->m_ctl.properties.begin(); pi != w->m_ctl.properties.end(); pi++) {
      OU::Property *p = *pi;
      if (p->m_isParameter)
	continue;
      if (p->m_isSequence) {
        fprintf(f, "  uint32_t %s_length;\n", p->m_name.c_str());
        if (p->m_align > sizeof(uint32_t))
          fprintf(f, "  char pad%u_[%zu];\n",
                  pad++, p->m_align - (unsigned)sizeof(uint32_t));
        align += p->m_align;
      }
      if (p->m_baseType == OA::OCPI_Struct) {
        fprintf(f, "  struct %c%s%c%s {\n",
                toupper(w->m_implName[0]), w->m_implName+1,
                toupper(p->m_name.c_str()[0]), p->m_name.c_str() + 1);
        emitStructOCL(f, p->m_nMembers, p->m_members, "  ");
        fprintf(f, "  } %s", p->m_name.c_str());
        if (p->m_isSequence)
          fprintf(f, "[%zu]", p->m_sequenceLength);
        fprintf(f, ";\n");
      } else
        printMember(f, p, "", align, pad);
    }

    fprintf(f,
            "\n} %c%sProperties;\n\n",
            toupper(w->m_implName[0]), w->m_implName + 1);
    }

  fprintf(f,
          "/*\n"
          " * Worker context structure for worker %s\n"
          " */\n"
          "typedef struct {\n",
          w->m_implName);

  if (w->m_ctl.properties.size()) {
    fprintf(f,"  __global %c%sProperties* properties;\n",
            toupper(w->m_implName[0]), w->m_implName + 1);
  }

  fprintf(f,"  OCLRunCondition runCondition;\n");

  if (w->m_localMemories.size()) {
    for (unsigned n = 0; n < w->m_localMemories.size(); n++) {
      LocalMemory* mem = w->m_localMemories[n];
      fprintf(f, "  __global void* %s;\n", mem->name );
    }
  }

  if (w->m_ports.size()) {
    for (unsigned n = 0; n < w->m_ports.size(); n++) {
      Port *port = w->m_ports[n];
      fprintf(f, "  OCLPort %s;\n", port->name );
      /* FIXME how do we deal with two-way ports */
    }
  }

  fprintf(f,
          "\n} OCLWorker%c%s;\n\n",
          toupper(w->m_implName[0]), w->m_implName + 1);

  fprintf(f,
          "\n"
          "#if defined (__cplusplus)\n"
          "}\n"
          "#endif\n"
          "#endif /* ifndef OCL_WORKER_%s_H__ */\n",
          upper);
  fclose(f);

  return 0;
}

const char*
emitSkelOCL(Worker *w, const char *outDir) {
  const char *err;
  FILE *f;
  if ((err = openOutput(w->m_fileName.c_str(), outDir, "", "_skel", ".cl", NULL, f)))
    return err;
  fprintf(f, "/*\n");
  printgen(f, " *", w->m_file.c_str(), true);
  fprintf(f, " *\n");
  fprintf(f,
	  " * This file contains the OCL implementation skeleton for worker: %s\n"
	  " */\n\n"
	  "#include \"%s_Worker.h\"\n\n"
	  "/*\n"
	  " * Required work group size for worker %s run() function.\n"
	  " */\n"
	  "#define OCL_WG_X 1\n"
	  "#define OCL_WG_Y 1\n"
	  "#define OCL_WG_Z 1\n\n"
	  "/*\n"
	  " * Methods to implement for worker %s, based on metadata.\n"
	  " */\n",
	  w->m_implName, w->m_implName, w->m_implName, w->m_implName);
  unsigned op = 0;
  const char **cp;
  const char *mName;
  for (cp = OU::Worker::s_controlOpNames; *cp; cp++, op++)
    if (w->m_ctl.controlOps & (1 << op)) {
      if ((err = methodName(w, *cp, mName)))
	return err;
      fprintf(f,
	      "\n"
	      "OCLResult %s_%s ( __local OCLWorker%c%s* self )\n{\n"
	      "\n  (void)self;\n"
	      "  return OCL_OK;\n"
	      "}\n",
	      w->m_implName,
	      mName,
        toupper(w->m_implName[0]),
        w->m_implName + 1 );
    }

  const size_t pad_len = 14 + strlen ( w->m_implName ) + 3;
  char pad [ pad_len + 1 ];
  memset ( pad, ' ', pad_len );
  pad [ pad_len ] = '\0';

  if ((err = methodName(w, "run", mName)))
    return err;
  fprintf(f,
	  "\n"
	  "OCLResult %s_run ( __local OCLWorker%c%s* self,\n"
	  "%sOCLBoolean timedOut,\n"
	  "%s__global OCLBoolean* newRunCondition )\n{\n"
	  "  (void)self;(void)timedOut;(void)newRunCondition;\n"
	  "  return OCL_ADVANCE;\n"
	  "}\n",
	  w->m_implName,
    toupper(w->m_implName[0]),
    w->m_implName + 1,
    pad,
    pad );

  fclose(f);

  return emitEntryPointOCL ( w, outDir );
}
/*
  FIXME
  1. Add local memory to metadata.
*/
const char *
emitArtOCL(Worker *aw, const char *outDir) {
  const char *err;
  FILE *f;
  if ((err = openOutput(aw->m_implName, outDir, "", "_art", ".xml", NULL, f)))
    return err;
  fprintf(f, "<!--\n");
  printgen(f, "", aw->m_file.c_str());
  fprintf(f,
	  " This file contains the artifact descriptor XML for the application assembly\n"
	  " named \"%s\". It must be attached (appended) to the shared object file\n",
	  aw->m_implName);
  fprintf(f, "  -->\n");
  // This assumes native compilation of course
  fprintf(f,
	  "<artifact os=\"%s\" osVersion=\"%s\" platform=\"%s\" "
	  "runtime=\"%s\" runtimeVersion=\"%s\" "
	  "tool=\"%s\" toolVersion=\"%s\">\n",
	  OCPI_CPP_STRINGIFY(OCPI_OS) + strlen("OCPI"),
	  OCPI_CPP_STRINGIFY(OCPI_OS_VERSION),
	  OCPI_CPP_STRINGIFY(OCPI_PLATFORM),
	  "", "", "", "");
#if 0
  // Define all workers
  for (WorkersIter wi = aw->m_assembly.m_workers.begin();
       wi != aw->m_assembly.m_workers.end(); wi++)
    emitWorker(f, *wi);
#else
  aw->emitWorkers(f);
#endif
  fprintf(f, "</artifact>\n");
  if (fclose(f))
    return "Could close output file. No space?";
  return 0;
}

static const char* emitEntryPointOCL ( Worker* w,
                                       const char* outDir )
{
  const char* err;
  FILE* f;
  if ((err = openOutput(w->m_fileName.c_str(), outDir, "", OCLENTRYPOINT, SOURCE, w->m_implName, f)))
    return err;
  fprintf(f, "\n\n/* ---- Generated code that dispatches to the worker's functions --------- */\n\n");

  fprintf ( f,
            "#ifndef DEFINED_OCPI_OCL_OPCODES\n"
            "#define DEFINED_OCPI_OCL_OPCODES 1\n"
            "typedef enum\n"
            "{\n"
            "  OCPI_OCL_INITIALIZE = 0,\n"
            "  OCPI_OCL_START,\n"
            "  OCPI_OCL_STOP,\n"
            "  OCPI_OCL_RELEASE,\n"
            "  OCPI_OCL_BEFORE_QUERY,\n"
            "  OCPI_OCL_AFTER_CONFIGURE,\n"
            "  OCPI_OCL_TEST,\n"
            "  OCPI_OCL_RUN\n\n"
            "} OcpiOclOpcodes_t;\n"
            "#endif\n\n" );

  fprintf ( f, "/* ----- Single function to dispatch both run() and control operations. -- */\n\n" );

  const size_t pad_len = 20 + strlen ( w->m_implName );
  char pad [ pad_len + 1 ];
  memset ( pad, ' ', pad_len );
  pad [ pad_len ] = '\0';

  fprintf ( f,
            "__kernel __attribute__((reqd_work_group_size(OCL_WG_X, OCL_WG_Y, OCL_WG_Z)))\n"
            "void %s_entry_point ( OcpiOclOpcodes_t opcode,\n"
            "%sOCLBoolean timedOut,\n"
            "%s__global void* properties,\n"
            "%s__global OCLRunCondition* runCondition,\n"
            "%s__global OCLBoolean* newRunCondition,\n",
            w->m_implName,
            pad,
            pad,
            pad,
            pad );

  if ( w->m_localMemories.size() )
  {
    for ( size_t n = 0; n < w->m_localMemories.size(); n++ )
    {
      fprintf ( f,
                "%s__global void* local_mem_%zu_data,\n",
                pad,
                n );
    }
  }

  if ( w->m_ports.size() )
  {
    for ( size_t n = 0; n < w->m_ports.size(); n++ )
    {
      Port* port = w->m_ports [ n ];
      fprintf ( f,
                "%s__global void* port_%s_data,\n"
                "%sunsigned int port_%s_max_length,\n"
                "%s__global OCLPortAttr* port_%s_attrs,\n",
                pad,
                port->name,
                pad,
                port->name,
                pad,
                port->name );
    }
  }

  fprintf ( f, "%s__global OCLResult* result )\n", pad );
  fprintf ( f, "{\n" );

  fprintf ( f, "  barrier ( CLK_GLOBAL_MEM_FENCE );\n\n" );

  fprintf ( f, "  /* ---- Only one worker runs control operations ------------------------ */\n\n" );

  fprintf ( f, "    if ( opcode != OCPI_OCL_RUN )\n" );
  fprintf ( f, "    {\n" );
  fprintf ( f, "      if ( get_global_id ( 0 ) != 0 )\n" );
  fprintf ( f, "      {\n" );
  fprintf ( f, "        return;\n" );
  fprintf ( f, "      }\n" );
  fprintf ( f, "    }\n\n" );

  fprintf ( f, "  /* ---- Aggregate the flattened arguments ------------------------------ */\n\n" );

  fprintf ( f, "  __local OCLWorker%c%s self;\n\n",
            toupper(w->m_implName[0]), w->m_implName + 1);

  fprintf ( f, "  /* ---- Initialize the property pointer -------------------------------- */\n\n" );

  if (w->m_ctl.properties.size())
  {
    fprintf ( f, "  self.properties = ( __global %c%sProperties* ) properties;\n\n",
              toupper ( w->m_implName [ 0 ] ),
              w->m_implName + 1 );
  }
  else
  {
    fprintf ( f, "  ( void ) properties;\n\n" );
  }

  fprintf ( f, "  /* ---- Initialize the run condition ----------------------------------- */\n\n" );

  fprintf ( f, "  self.runCondition = *runCondition;\n\n" );

  if ( w->m_localMemories.size() )
  {
    fprintf ( f, "  /* ---- Initialize the local memory structures ------------------------- */\n\n" );
    for ( size_t n = 0; n < w->m_localMemories.size(); n++ )
    {
      fprintf ( f,
                "  self.%s = local_mem_%zu_data;\n",
                w->m_localMemories [ n ]->name,
                n );
    }
    fprintf ( f, "\n" );
  }

  fprintf ( f, "  /* ---- Initialize the port structures --------------------------------- */\n\n" );

  if ( w->m_ports.size() )
  {
    for ( size_t n = 0; n < w->m_ports.size(); n++ )
    {
      Port* port = w->m_ports [ n ];
      fprintf ( f,
                "  self.%s.current.data = port_%s_data;\n"
                "  self.%s.current.maxLength = port_%s_max_length;\n"
                "  self.%s.attr.length = port_%s_attrs->length;\n"
                "  self.%s.attr.connected = port_%s_attrs->connected;\n"
                "  self.%s.attr.u.operation = port_%s_attrs->u.operation;\n\n",
                port->name,
                port->name,
                port->name,
                port->name,
                port->name,
                port->name,
                port->name,
                port->name,
                port->name,
                port->name );
    }
  }

  fprintf ( f, "  /* ---- Perform the actual operation ----------------------------------- */\n\n" );

  fprintf ( f, "  barrier ( CLK_LOCAL_MEM_FENCE );\n\n" );

  fprintf ( f, "  OCLResult rc = 0;\n\n" );

  fprintf ( f, "  switch ( opcode )\n" );
  fprintf ( f, "  {\n" );

  fprintf ( f, "    case OCPI_OCL_RUN:\n" );
  fprintf ( f, "      rc = %s_run ( &self, timedOut, newRunCondition );\n", w->m_implName );
  fprintf ( f, "      break;\n" );
  unsigned op = 0;
  const char* mName;
  for ( const char** cp = OU::Worker::s_controlOpNames; *cp; cp++, op++ )
  {
    if ( w->m_ctl.controlOps & (1 << op ) )
    {
      if ( ( err = methodName ( w, *cp, mName  ) ) )
      {
        return err;
      }
      const char* mUname = upperdup ( mName );
      fprintf ( f, "    case OCPI_OCL_%s:\n", mUname );
      fprintf ( f, "      rc = %s_%s ( &self );\n",
                   w->m_implName,
                   mName );
      fprintf ( f, "      break;\n" );
    }
  }
  fprintf ( f, "  }\n\n" );

  fprintf ( f, "  /* ---- Update the run condition --------------------------------------- */\n\n" );

  fprintf ( f, "  if ( *newRunCondition )\n" );
  fprintf ( f, "  {\n" );
  fprintf ( f, "    *runCondition = self.runCondition;\n" );
  fprintf ( f, "  }\n\n" );

  fprintf ( f, "  /* ---- Update the output ports ---------------------------------------- */\n\n" );

  if ( w->m_ports.size() )
  {
    for ( size_t n = 0; n < w->m_ports.size(); n++ )
    {
      Port* port = w->m_ports [ n ];

      if ( port->u.wdi.isProducer ) {
        fprintf ( f,
                  "  port_%s_attrs->length = self.%s.attr.length;\n"
                  "  port_%s_attrs->u.operation = self.%s.attr.u.operation;\n\n",
                  port->name,
                  port->name,
                  port->name,
                  port->name );
      }
    }
  }

  fprintf ( f, "  /* ---- Return worker function result (ok, done, advance) -------------- */\n\n" );

  fprintf ( f, "  *result = rc;\n\n" );

  fprintf ( f, "  barrier ( CLK_LOCAL_MEM_FENCE );\n" );
  fprintf ( f, "  barrier ( CLK_GLOBAL_MEM_FENCE );\n\n" );

  fprintf ( f, "}\n\n" );

  if (fclose(f))
    return "Could close output file. No space?";
  return 0;
}
