#include "wrapper.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>


const char* version(void)
{
	return DIST_VERSION;
}

/*--------------------------------------------------------------------------*/

RdKafkaCore::RdKafkaCore(void)
 : m_uiReferenceCounter(0)
 , m_ptRk(NULL)
 , m_uiFailures(0)
{
}



RdKafkaCore::~RdKafkaCore(void)
{
	if( m_ptRk!=NULL )
	{
		rd_kafka_destroy(m_ptRk);
		rd_kafka_wait_destroyed(1000);
		m_ptRk = NULL;
	}
}



void RdKafkaCore::createCore(const char *pcBrokerList, lua_State *ptLuaState, lua_State *ptLuaStateForConfig, int iConfigTableIndex)
{
	rd_kafka_conf_t *ptConf;
	rd_kafka_t *ptRk;
	int iResult;
	char acError[512];


	ptConf = rd_kafka_conf_new();
	iResult = 0;
	if( ptLuaStateForConfig!=NULL )
	{
		iResult = load_conf(ptLuaState, ptConf, iConfigTableIndex);
	}
	if( iResult!=0 )
	{
		rd_kafka_conf_destroy(ptConf);
		luaL_error(ptLuaState, "Failed to read the config.");
	}
	else
	{
		rd_kafka_conf_set_opaque(ptConf, this);
		rd_kafka_conf_set_dr_msg_cb(ptConf, RdKafkaCore::messageCallbackStatic);

		rd_kafka_conf_set_log_cb(ptConf, NULL); // disable logging
		rd_kafka_conf_set_stats_cb(ptConf, NULL); // disable stats

		ptRk = rd_kafka_new(RD_KAFKA_PRODUCER, ptConf, acError, sizeof(acError));
		if( ptRk==NULL )
		{
			rd_kafka_conf_destroy(ptConf); // the producer has not taken ownership
			fprintf(stderr, "rd_kafka_new failed: %s\n", acError);
			luaL_error(ptLuaState, "rd_kafka_new failed: %s", acError);
		}
		else
		{
			if( rd_kafka_brokers_add(ptRk, pcBrokerList)==0 )
			{
				luaL_error(ptLuaState, "invalid broker list");
			}
			else
			{
				m_ptRk = ptRk;
			}
		}
	}
}



void RdKafkaCore::reference(void)
{
	++m_uiReferenceCounter;
	printf("RdKafkaCore(%p): Increasing the reference count to %d.\n", this, m_uiReferenceCounter);
}



void RdKafkaCore::dereference(void)
{
	--m_uiReferenceCounter;
	printf("RdKafkaCore(%p): Decreasing the reference count to %d.\n", this, m_uiReferenceCounter);
	if( m_uiReferenceCounter==0 )
	{
		printf("RdKafkaCore(%p): All references gone, deleting.\n", this);
		delete this;
	}
}



void RdKafkaCore::messageCallbackStatic(rd_kafka_t *ptRk, const rd_kafka_message_t *ptRkMessage, void *pvOpaque)
{
	RdKafkaCore *ptThis;


	ptThis = (RdKafkaCore*)pvOpaque;
	ptThis->messageCallback(ptRk, ptRkMessage);
}



void RdKafkaCore::messageCallback(rd_kafka_t *ptRk, const rd_kafka_message_t *ptRkMessage)
{
	m_pvMsgOpaque = ptRkMessage->_private;
	if( ptRkMessage->err!=RD_KAFKA_RESP_ERR_NO_ERROR )
	{
		++m_uiFailures;
	}
}



rd_kafka_t *RdKafkaCore::_getRk(void)
{
	return m_ptRk;
}



void RdKafkaCore::poll(int iTimeout, void **ppvMsgOpaque, unsigned int *puiFailures)
{
	m_pvMsgOpaque = NULL;
	m_uiFailures = 0;

	rd_kafka_poll(m_ptRk, iTimeout);

	*ppvMsgOpaque = m_pvMsgOpaque;
	*puiFailures = m_uiFailures;
}



int RdKafkaCore::load_conf(lua_State *lua, rd_kafka_conf_t *conf, int idx)
{
  if (!conf) {
    printf("rd_kafka_conf_new() failed, conf is NULL\n");
    return -1;
  }
  if (lua_isnil(lua, idx)) {
    return 0;
  }

  char errstr[512];
  lua_pushnil(lua);
  while (lua_next(lua, idx) != 0) {
    int kt = lua_type(lua, -2);
    int vt = lua_type(lua, -1);
    switch (kt) {
    case LUA_TSTRING:
      switch (vt) {
      case LUA_TSTRING:
        {
          const char *key = lua_tostring(lua, -2);
          const char *value = lua_tostring(lua, -1);
          if (value) {
            rd_kafka_conf_res_t r;
            r = rd_kafka_conf_set(conf, key, value, errstr, sizeof errstr);
            if (r) {
              printf("Failed to set %s = %s : %s", key, value,
                              errstr);
              return -1;
            }
          }
        }
        break;
      case LUA_TNUMBER:
        {
          const char *key = lua_tostring(lua, -2);
          int i = (int)lua_tointeger(lua, -1);
          char value[12];
          snprintf(value, sizeof value, "%d", i);
          rd_kafka_conf_res_t r;
          r = rd_kafka_conf_set(conf, key, value, errstr, sizeof errstr);
          if (r) {
            printf("Failed to set %s = %s : %s", key, value,
                            errstr);
            return -1;
          }
        }
        break;
      case LUA_TBOOLEAN:
        {
          const char *key = lua_tostring(lua, -2);
          const char *value = "false";
          if (lua_toboolean(lua, -1)) {
            value = "true";
          }
          rd_kafka_conf_res_t r;
          r = rd_kafka_conf_set(conf, key, value, errstr, sizeof errstr);
          if (r) {
            printf("Failed to set %s = %s : %s", key, value,
                            errstr);
            return -1;
          }
        }
        break;
      default:
        printf("invalid config value type: %s",
                        lua_typename(lua, vt));
        return -1;
      }
      break;
    default:
      printf("invalid config key type: %s",
                      lua_typename(lua, kt));
      return -1;
    }
    lua_pop(lua, 1);
  }
  return 0;
}


/*--------------------------------------------------------------------------*/

Topic::Topic(RdKafkaCore *ptCore, lua_State *ptLuaState, const char *pcTopic, lua_State *ptLuaStateForConfig, int iConfigTableIndex)
 : m_ptCore(NULL)
 , m_ptTopic(NULL)
{
	rd_kafka_topic_conf_t *ptConf;
	int iResult;


	ptConf = rd_kafka_topic_conf_new();
	if( ptLuaStateForConfig!=NULL )
	{
		iResult = load_topic_conf(ptLuaStateForConfig, ptConf, iConfigTableIndex);
		if( iResult!=0 )
		{
			rd_kafka_topic_conf_destroy(ptConf);
			lua_error(ptLuaState);
		}
	}

	m_ptTopic = rd_kafka_topic_new(ptCore->_getRk(), pcTopic, ptConf);
	if( m_ptTopic==NULL )
	{
		rd_kafka_topic_conf_destroy(ptConf);
		luaL_error(ptLuaState, "rd_kafka_topic_new failed");
	}
	
	m_ptCore = ptCore;
	m_ptCore->reference();
}



Topic::~Topic(void)
{
	if( m_ptTopic!=NULL )
	{
		rd_kafka_topic_destroy(m_ptTopic);
	}

	if( m_ptCore!=NULL )
	{
		m_ptCore->dereference();
	}
}



int Topic::send(int iPartition, uintptr_t uiSequenceNr, const char *pcMessage)
{
	int iResult;
	size_t sizMessage;
	void *pvOpaque;


	/* Get the size of the message. */
	sizMessage = strlen(pcMessage);

	pvOpaque = (void*)uiSequenceNr;

	errno = 0;
	iResult = rd_kafka_produce(m_ptTopic, iPartition, RD_KAFKA_MSG_F_COPY, (void*)pcMessage, sizMessage, NULL, 0, pvOpaque);
	if( iResult==-1 )
	{
		iResult = errno;
	}
	else
	{
		iResult = 0;
	}

	return iResult;
}



void Topic::poll(uintptr_t *puiUINT_OR_NIL, unsigned int *puiUINT_OUT, int iTimeout)
{
	void *pvMsgOpaque;
	unsigned int uiFailures;


	m_ptCore->poll(iTimeout, &pvMsgOpaque, &uiFailures);

	*puiUINT_OR_NIL = (uintptr_t)pvMsgOpaque;
	*puiUINT_OUT = uiFailures;
}



int Topic::load_topic_conf(lua_State *ptLua, rd_kafka_topic_conf_t *ptConf, int idx)
{
  if (!ptConf) {
    lua_pushstring(ptLua, "rd_kafka_topic_conf_new() failed");
    return -1;
  }
  if (lua_isnil(ptLua, idx)) {
    return 0;
  }

  char errstr[512];
  lua_pushnil(ptLua);
  while (lua_next(ptLua, idx) != 0) {
    int kt = lua_type(ptLua, -2);
    int vt = lua_type(ptLua, -1);
    switch (kt) {
    case LUA_TSTRING:
      switch (vt) {
      case LUA_TSTRING:
        {
          const char *key = lua_tostring(ptLua, -2);
          const char *value = lua_tostring(ptLua, -1);
          if (value) {
            rd_kafka_conf_res_t r;
            r = rd_kafka_topic_conf_set(ptConf, key, value, errstr,
                                        sizeof errstr);
            if (r) {
              lua_pushfstring(ptLua, "Failed to set %s = %s : %s", key, value,
                              errstr);
              return -1;
            }
          }
        }
        break;
      case LUA_TNUMBER:
        {
          const char *key = lua_tostring(ptLua, -2);
          int i = (int)lua_tointeger(ptLua, -1);
          char value[12];
          snprintf(value, sizeof value, "%d", i);
          rd_kafka_conf_res_t r;
          r = rd_kafka_topic_conf_set(ptConf, key, value, errstr,
                                      sizeof errstr);
          if (r) {
            lua_pushfstring(ptLua, "Failed to set %s = %s : %s", key, value,
                            errstr);
            return -1;
          }
        }
        break;
      case LUA_TBOOLEAN:
        {
          const char *key = lua_tostring(ptLua, -2);
          const char *value = "false";
          if (lua_toboolean(ptLua, -1)) {
            value = "true";
          }
          rd_kafka_conf_res_t r;
          r = rd_kafka_topic_conf_set(ptConf, key, value, errstr,
                                      sizeof errstr);
          if (r) {
            lua_pushfstring(ptLua, "Failed to set %s = %s : %s", key, value,
                            errstr);
            return -1;
          }
        }
        break;
      default:
        lua_pushfstring(ptLua, "invalid config value type: %s",
                        lua_typename(ptLua, vt));
        return -1;
      }
      break;
    default:
      lua_pushfstring(ptLua, "invalid config key type: %s",
                      lua_typename(ptLua, kt));
      return -1;
    }
    lua_pop(ptLua, 1);
  }
  return 0;
}



/*--------------------------------------------------------------------------*/

Producer::Producer(lua_State *MUHKUH_LUA_STATE, const char *pcBrokerList, lua_State *ptLuaStateForTableAccessOptional)
 : m_ptCore(NULL)
{
	if( ptLuaStateForTableAccessOptional==NULL )
	{
		printf("Producer(%p) created without config.\n", this);
	}
	else
	{
		printf("Producer(%p) created with config.\n", this);
	}

	/* Create a new core. */
	m_ptCore = new RdKafkaCore();
	if( m_ptCore!=NULL )
	{
		m_ptCore->createCore(pcBrokerList, MUHKUH_LUA_STATE, ptLuaStateForTableAccessOptional, 2);
		m_ptCore->reference();
	}
}



Producer::~Producer(void)
{
	if( m_ptCore!=NULL )
	{
		m_ptCore->dereference();
		m_ptCore = NULL;
	}

	printf("Producer(%p) deleted.\n", this);
}



void Producer::poll(uintptr_t *puiUINT_OR_NIL, unsigned int *puiUINT_OUT, int iTimeout)
{
	void *pvMsgOpaque;
	unsigned int uiFailures;


	m_ptCore->poll(iTimeout, &pvMsgOpaque, &uiFailures);

	*puiUINT_OR_NIL = (uintptr_t)pvMsgOpaque;
	*puiUINT_OUT = uiFailures;
}



Topic *Producer::create_topic(lua_State *MUHKUH_LUA_STATE, const char *pcTopic, lua_State *ptLuaStateForTableAccessOptional)
{
	Topic *ptTopic;


	if( ptLuaStateForTableAccessOptional==NULL )
	{
		printf("Producer(%p) create_topic without config.\n", this);
	}
	else
	{
		printf("Producer(%p) create_topic with config.\n", this);
	}

	ptTopic = new Topic(m_ptCore, MUHKUH_LUA_STATE, pcTopic, ptLuaStateForTableAccessOptional, 3);
	return ptTopic;
}

