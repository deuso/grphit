#pragma once

#include "mem_component.h"
#include "fixed_types.h"
#include "../shmem_msg.h"

namespace PrL1ShL2SpDirMSI
{

class ShmemMsg : public ::ShmemMsg
{
public:
   enum Type
   {
      INVALID_MSG_TYPE = 0,
      MIN_MSG_TYPE,
      EX_REQ = MIN_MSG_TYPE,
      SH_REQ,
      INV_REQ,
      FLUSH_REQ,
      WB_REQ,
      EX_REP,
      SH_REP,
      UPGRADE_REP,
      INV_REP,
      FLUSH_REP,
      WB_REP,
      // Dram requests
      DRAM_FETCH_REQ,
      DRAM_STORE_REQ,
      DRAM_FETCH_REP,
      // Nullify req
      NULLIFY_REQ,
      // SpDir-L2 requests
      SPDIR_RD_REQ,
      SPDIR_WR_REQ,
      SPDIR_RD_REP,
      SPDIR_WR_REP,

      L2_SPDIR_REQ,
      L2_SPDIR_REP,

      MAX_MSG_TYPE = L2_SPDIR_REP,
      NUM_MSG_TYPES = MAX_MSG_TYPE - MIN_MSG_TYPE + 1
   }; 

   ShmemMsg();
   ShmemMsg(Type msg_type
            , MemComponent::Type sender_mem_component
            , MemComponent::Type receiver_mem_component
            , tile_id_t requester
            , bool reply_expected
            , IntPtr address
            , bool modeled
            );
   ShmemMsg(Type msg_type
            , MemComponent::Type sender_mem_component
            , MemComponent::Type receiver_mem_component
            , tile_id_t requester
            , bool reply_expected
            , IntPtr address
            , Byte* data_buf
            , UInt32 data_length
            , bool modeled
            );
   ShmemMsg(const ShmemMsg* shmem_msg);
   ~ShmemMsg();

   void clone(const ShmemMsg* shmem_msg);
   static ShmemMsg* getShmemMsg(Byte* msg_buf);
   Byte* makeMsgBuf();
   UInt32 getMsgLen();

   // Modeled Parameters
   UInt32 getModeledLength();

   Type getType() const                               { return _msg_type; }
   MemComponent::Type getSenderMemComponent() const   { return _sender_mem_component; }
   MemComponent::Type getReceiverMemComponent() const { return _receiver_mem_component; }
   tile_id_t getRequester() const                     { return _requester; }
   bool isReplyExpected() const                       { return _reply_expected; }
   IntPtr getAddress() const                          { return _address; }
   Byte* getDataBuf() const                           { return _data_buf; }
   UInt32 getDataLength() const                       { return _data_length; }
   bool isModeled() const                             { return _modeled; }

   void setMsgType(Type msg_type)                     { _msg_type = msg_type; }
   void setDataBuf(Byte* data_buf)                    { _data_buf = data_buf; }
   void setDataLen(UInt32 len)                    { _data_length = len; }

private:   
   Type _msg_type;
   MemComponent::Type _sender_mem_component;
   MemComponent::Type _receiver_mem_component;
   tile_id_t _requester;
   bool _reply_expected;
   IntPtr _address;
   Byte* _data_buf;
   UInt32 _data_length;
   bool _modeled;
   
   static const UInt32 _num_msg_type_bits = 4;
};

}
