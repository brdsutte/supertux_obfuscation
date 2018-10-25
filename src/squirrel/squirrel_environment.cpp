//  SuperTux
//  Copyright (C) 2018 Ingo Ruhnke <grumbel@gmail.com>
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include "squirrel/squirrel_environment.hpp"

#include <algorithm>

#include "squirrel/script_interface.hpp"
#include "squirrel/squirrel_virtual_machine.hpp"
#include "squirrel/squirrel_error.hpp"
#include "squirrel/squirrel_util.hpp"
#include "supertux/game_object.hpp"
#include "util/log.hpp"

SquirrelEnvironment::SquirrelEnvironment(HSQUIRRELVM vm, const std::string& name) :
  m_vm(vm),
  m_table(),
  m_name(name),
  m_scripts()
{
  // garbage collector has to be invoked manually
  sq_collectgarbage(m_vm);

  sq_newtable(m_vm);
  sq_pushroottable(m_vm);
  if(SQ_FAILED(sq_setdelegate(m_vm, -2)))
    throw SquirrelError(m_vm, "Couldn't set table delegate");

  sq_resetobject(&m_table);
  if (SQ_FAILED(sq_getstackobj(m_vm, -1, &m_table))) {
    throw SquirrelError(m_vm, "Couldn't get table");
  }

  sq_addref(m_vm, &m_table);
  sq_pop(m_vm, 1);
}

SquirrelEnvironment::~SquirrelEnvironment()
{
  for(auto& script: m_scripts)
  {
    sq_release(m_vm, &script);
  }
  m_scripts.clear();
  sq_release(m_vm, &m_table);

  sq_collectgarbage(m_vm);
}

void
SquirrelEnvironment::expose_self()
{
  sq_pushroottable(m_vm);
  store_object(m_vm, m_name.c_str(), m_table);
  sq_pop(m_vm, 1);
}

void
SquirrelEnvironment::unexpose_self()
{
  sq_pushroottable(m_vm);
  delete_table_entry(m_vm, m_name.c_str());
  sq_pop(m_vm, 1);
}

void
SquirrelEnvironment::try_expose(GameObject& object)
{
  auto script_object = dynamic_cast<ScriptInterface*>(&object);
  if (script_object != nullptr) {
    sq_pushobject(m_vm, m_table);
    script_object->expose(m_vm, -1);
    sq_pop(m_vm, 1);
  }
}

void
SquirrelEnvironment::try_unexpose(GameObject& object)
{
  auto script_object = dynamic_cast<ScriptInterface*>(&object);
  if (script_object != nullptr) {
    SQInteger oldtop = sq_gettop(m_vm);
    sq_pushobject(m_vm, m_table);
    try {
      script_object->unexpose(m_vm, -1);
    } catch(std::exception& e) {
      log_warning << "Couldn't unregister object: " << e.what() << std::endl;
    }
    sq_settop(m_vm, oldtop);
  }
}

void
SquirrelEnvironment::unexpose(const std::string& name)
{
  SQInteger oldtop = sq_gettop(m_vm);
  sq_pushobject(m_vm, m_table);
  try {
    unexpose_object(m_vm, -1, name.c_str());
  } catch(std::exception& e) {
    log_warning << "Couldn't unregister object: " << e.what() << std::endl;
  }
  sq_settop(m_vm, oldtop);
}

void
SquirrelEnvironment::run_script(const std::string& script, const std::string& sourcename)
{
  if (script.empty()) return;

  std::istringstream stream(script);
  run_script(stream, sourcename);
}

void
SquirrelEnvironment::garbage_collect()
{
  m_scripts.erase(
    std::remove_if(m_scripts.begin(), m_scripts.end(),
                   [this](HSQOBJECT& object){
                     HSQUIRRELVM vm = object_to_vm(object);

                     if(sq_getvmstate(vm) != SQ_VMSTATE_SUSPENDED) {
                       sq_release(m_vm, &object);
                       return true;
                     } else {
                       return false;
                     }
                   }),
    m_scripts.end());
}

void
SquirrelEnvironment::run_script(std::istream& in, const std::string& sourcename)
{
  garbage_collect();

  try
  {
    HSQOBJECT object = create_thread(m_vm);
    m_scripts.push_back(object);

    HSQUIRRELVM vm = object_to_vm(object);

    sq_setforeignptr(vm, SquirrelVirtualMachine::current());

    // set root table
    sq_pushobject(vm, m_table);
    sq_setroottable(vm);

    compile_and_run(vm, in, sourcename);
  }
  catch(const std::exception& e)
  {
    log_warning << "Error running script: " << e.what() << std::endl;
  }
}

/* EOF */