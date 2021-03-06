
/*
 *  Copyright (c) Mercury Federal Systems, Inc., Arlington VA., 2009-2010
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

#define __STDC_LIMIT_MACROS
#include <stdint.h>
#include <string.h>
#include "OcpiOsAssert.h"
#include "OcpiUtilMisc.h"
#include "OcpiUtilEzxml.h"
#include "OcpiUtilImplementation.h"

namespace OCPI {
  namespace Util {

    namespace OE = OCPI::Util::EzXml;
    Worker::Worker()
      : m_ports(0), m_memories(0), // m_tests(0), m_nTests(0), 
	m_nPorts(0), m_nMemories(0),// size(0),
        m_totalPropertySize(0), m_nProperties(0), m_properties(0), m_xml(NULL)
    {}

    Worker::~Worker() {
      delete [] m_ports;
      delete [] m_properties;
      //      delete [] m_tests;
      delete [] m_memories;
    }
    unsigned Worker::whichProperty(const char *id) const {
      Property *p = m_properties;
      for (unsigned n=0; n < m_nProperties; n++, p++)
        if (!strcasecmp(p->m_name.c_str(), id))
          return n;
      throw Error("Unknown property: \"%s\" for worker \"%s\"", id, m_specName.c_str());
    }
    Property &Worker::findProperty(const char *id) const {
      return *(m_properties + whichProperty(id));
    }
    Port *Worker::findMetaPort(const char *id) const {
      Port *p = m_ports;
      for (unsigned int n = m_nPorts; n; n--, p++)
        if (!strcasecmp(p->m_name.c_str(), id))
          return p;
      return NULL;
    }
#if 0
    Test &Worker::findTest(unsigned int testId) const {
       (void)testId;
       ocpiAssert(0); return *m_tests;
    }
#endif
    const char *Worker::parse(ezxml_t xml, Attributes *attr) {
      m_attributes = attr;
      const char *err = OE::getRequiredString(xml, m_name, "name", "worker");
      if (err ||
	  (err = OE::getRequiredString(xml, m_model, "model", "worker")))
	return err;
      OE::getOptionalString(xml, m_specName, "specName");
      if (m_specName.empty())
	m_specName = m_name;
      if ((m_nProperties = OE::countChildren(xml, "property")))
	m_properties = new Property[m_nProperties];
      if ((m_nPorts = OE::countChildren(xml, "port")))
	m_ports = new Port[m_nPorts];
      m_nMemories = OE::countChildren(xml, "localMemory");
      if ((m_nMemories += OE::countChildren(xml, "memory")))
        m_memories = new Memory[m_nMemories];
      // Second pass - decode all information
      Property *prop = m_properties;
      ezxml_t x;
      bool readableConfigs, writableConfigs, sub32Configs; // all unused
      for (x = ezxml_cchild(xml, "property"); x; x = ezxml_next(x), prop++)
        if ((err = prop->parse(x, readableConfigs, writableConfigs,
			       sub32Configs, true, (unsigned)(prop - m_properties))))
          return esprintf("Invalid xml property description: %s", err);
      prop = m_properties;
      size_t offset = 0;
      uint64_t totalSize = 0;
      for (unsigned n = 0; n < m_nProperties; n++, prop++)
	prop->offset(offset, totalSize);
      ocpiAssert(totalSize < UINT32_MAX);
      m_totalPropertySize = OCPI_UTRUNCATE(size_t, totalSize);
      // Ports at this level are unidirectional? Or do we support the pairing at this point?
      unsigned n = 0;
      Port *p = m_ports;
      for (x = ezxml_cchild(xml, "port"); x; x = ezxml_next(x), p++, n++)
        if ((err = p->parse(x, n)))
          return esprintf("Invalid xml port description: %s", err);
      Memory* m = m_memories;
      for (x = ezxml_cchild(xml, "memory"); x; x = ezxml_next(x), m++ )
        if ((err = m->parse(x)))
          return esprintf("Invalid xml local memory description: %s", err);
      for (x = ezxml_cchild(xml, "memory"); x; x = ezxml_next(x), m++ )
        if ((err = m->parse(x)))
          return esprintf("Invalid xml local memory description: %s", err);
      m_xml = xml;
      return NULL;
    }
    // Get a property value from the metadata
    const char *Worker::getValue(const std::string &sym, ExprValue &val) {
      // Our builtin symbols take precendence, but can be overridden with $
      if (sym == "model") {
	val.isNumber = false;
	val.string = m_model;
	return NULL;
      } else if (sym == "platform" && m_attributes) {
	val.isNumber = false;
	val.string = m_attributes->m_platform;
	return NULL;
      } else if (sym == "os" && m_attributes) {
	val.isNumber = false;
	val.string = m_attributes->m_os;
	return NULL;
      }
      Property *p = m_properties;
      const char *cSym = sym.c_str();
      if (cSym[0] == '@')
	cSym++;
      for (unsigned n = 0; n < m_nProperties; n++, p++)
	if (p->m_name == cSym)
	  return p->getValue(val);
      return esprintf("no property found for identifier \"%s\"", cSym);
    }

    void parse3(char *s, std::string &s1, std::string &s2,
		std::string &s3) {
      char *temp = strdup(s);
      if ((s = strsep(&temp, "-"))) {
	s1 = s;
	if ((s = strsep(&temp, "-"))) {
	  s2 = s;
	  if ((s = strsep(&temp, "-")))
	    s3 = s;
	}
      }
      free(temp);
    }

    void Attributes::parse(ezxml_t x) {
      const char *cp;
      if ((cp = ezxml_cattr(x, "os"))) m_os = cp;
      if ((cp = ezxml_cattr(x, "osVersion"))) m_osVersion = cp;
      if ((cp = ezxml_cattr(x, "platform"))) m_platform = cp;
      if ((cp = ezxml_cattr(x, "runtime"))) m_runtime = cp;
      if ((cp = ezxml_cattr(x, "runtimeVersion"))) m_runtimeVersion = cp;
      if ((cp = ezxml_cattr(x, "tool"))) m_tool = cp;
      if ((cp = ezxml_cattr(x, "toolVersion"))) m_toolVersion = cp;
      if ((cp = ezxml_cattr(x, "uuid"))) m_uuid = cp;
      validate();
    }

    void Attributes::parse(const char *pString) {
      std::string junk;
      char *p = strdup(pString), *temp = p, *val;
      
      if ((val = strsep(&temp, "="))) {
	parse3(val, m_os, m_osVersion, junk);
	if ((val = strsep(&temp, "="))) {
	  parse3(val, m_platform, junk, junk);
	  if ((val = strsep(&temp, "="))) {
	    parse3(val, m_tool, m_toolVersion, junk);
	    if ((val = strsep(&temp, "=")))
	      parse3(val, m_runtime, m_runtimeVersion, junk);
	  }
	}
      }
      free(p);
      validate();
    }
    void Attributes::validate() { }
    const char *Worker::s_controlOpNames[] = {
#define CONTROL_OP(x, c, t, s1, s2, s3, s4)  #x,
          OCPI_CONTROL_OPS
#undef CONTROL_OP
	  NULL
    };
    const char *Worker::s_controlStateNames[] = {
#define CONTROL_STATE(s) #s,
      OCPI_CONTROL_STATES
#undef CONTROL_STATE
      NULL
    };      
  }
}

