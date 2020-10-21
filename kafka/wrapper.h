#include <librdkafka/rdkafka.h>

#ifdef __cplusplus
extern "C" {
#endif
#include "lua.h"
#ifdef __cplusplus
}
#endif


#include <stdint.h>



#ifndef SWIGRUNTIME
#include <swigluarun.h>
#endif



#ifndef __WRAPPER_H__
#define __WRAPPER_H__


typedef int RESULT_UINT;


const char* version(void);



/* Do not wrap the core class, it can not be accessed directly from LUA. */
#ifndef SWIG
class RdKafkaCore
{
public:
	RdKafkaCore(void);
	~RdKafkaCore(void);

	void createCore(const char *pcBrokerList, lua_State *ptLuaState, lua_State *ptLuaStateForConfig, int iConfigTableIndex);

	void reference(void);
	void dereference(void);

	static void messageCallbackStatic(rd_kafka_t *ptRk, const rd_kafka_message_t *ptRkMessage, void *pvOpaque);
	void messageCallback(rd_kafka_t *ptRk, const rd_kafka_message_t *ptRkMessage);

	rd_kafka_t *_getRk(void);

	void poll(int iTimeout, void **ppvMsgOpaque, unsigned int *puiFailures);
private:
	int load_conf(lua_State *lua, rd_kafka_conf_t *conf, int idx);

	unsigned int m_uiReferenceCounter;
	rd_kafka_t *m_ptRk;
	unsigned int m_uiFailures;
	void *m_pvMsgOpaque;
};
#endif



class Topic
{
public:
	Topic(RdKafkaCore *ptCore, lua_State *ptLuaState, const char *pcTopic, lua_State *ptLuaStateForConfig, int iConfigTableIndex);
	~Topic(void);

	RESULT_UINT send(int iPartition, uintptr_t uiSequenceNr, const char *pcMessage);

	void poll(uintptr_t *puiUINT_OR_NIL, unsigned int *puiUINT_OUT, int iTimeout=0);

private:
	int load_topic_conf(lua_State *ptLua, rd_kafka_topic_conf_t *ptConf, int idx);

	RdKafkaCore *m_ptCore;
	rd_kafka_topic_t *m_ptTopic;
};



class Producer
{
public:
	Producer(lua_State *MUHKUH_LUA_STATE, const char *pcBrokerList, lua_State *ptLuaStateForTableAccessOptional);
	~Producer(void);

	Topic *create_topic(lua_State *MUHKUH_LUA_STATE, const char *pcTopic, lua_State *ptLuaStateForTableAccessOptional);

#ifndef SWIG
private:
	RdKafkaCore *m_ptCore;
#endif
};

#endif  /* __WRAPPER_H__ */
