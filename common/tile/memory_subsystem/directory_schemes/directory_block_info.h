#pragma once

#include "directory_state.h"

class DirectoryBlockInfo
{
private:
   DirectoryState::Type _dstate;
   bool _inst;

public:
   DirectoryBlockInfo(DirectoryState::Type dstate = DirectoryState::UNCACHED, bool inst=false)
      : _dstate(dstate), _inst(inst) {}
   ~DirectoryBlockInfo() {}

   DirectoryState::Type getDState() { return _dstate; }
   void setDState(DirectoryState::Type dstate) { _dstate = dstate; }
   bool getInst() { return _inst; }
   void setInst(bool inst) { _inst = inst; }
};
