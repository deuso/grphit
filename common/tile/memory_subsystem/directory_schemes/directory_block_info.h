#pragma once

#include "directory_state.h"

class DirectoryBlockInfo
{
private:
   DirectoryState::Type _dstate;
   bool _region;

public:
   DirectoryBlockInfo(DirectoryState::Type dstate = DirectoryState::UNCACHED, bool region=false)
   //DirectoryBlockInfo(DirectoryState::Type dstate = DirectoryState::UNCACHED)
      : _dstate(dstate) {}
   ~DirectoryBlockInfo() {}

   DirectoryState::Type getDState() { return _dstate; }
   void setDState(DirectoryState::Type dstate) { _dstate = dstate; }
   bool isRegion() { return _region; }
   void setRegion(bool region) { _region= region; }
};
