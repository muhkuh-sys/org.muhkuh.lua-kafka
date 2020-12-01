#include "wrapper.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>


const char* version(void)
{
	return DIST_VERSION;
}



void kafka_initialize_error_codes(lua_State *ptLuaState)
{
	int iTop;
	int iType;
	size_t sizErrDescs;
	const struct rd_kafka_err_desc *ptCnt;
	const struct rd_kafka_err_desc *ptEnd;
	const char *pcName;

	/* Get the global "kafka". */
	iTop = lua_gettop(ptLuaState);
	lua_getglobal(ptLuaState, "kafka");
	iType = lua_type(ptLuaState, iTop+1);
	if( iType==LUA_TTABLE )
	{
		/* Store the table name for later. */
		lua_pushstring(ptLuaState, "RD_KAFKA_RESP_ERR");
		/* Create a new table. */
		lua_newtable(ptLuaState);

		rd_kafka_get_err_descs(&ptCnt, &sizErrDescs);
		ptEnd = ptCnt + sizErrDescs;
		while( ptCnt<ptEnd )
		{
			pcName = ptCnt->name;
			if( pcName!=NULL )
			{
				lua_pushstring(ptLuaState, pcName);
#if LUA_VERSION_NUM>=504
				lua_pushinteger(ptLuaState, ptCnt->code);
#else
				lua_pushnumber(ptLuaState, ptCnt->code);
#endif
				lua_rawset(ptLuaState, iTop+3);
			}

			++ptCnt;
		}

		/* Add the errors to the "kafka" table. */
		lua_rawset(ptLuaState, iTop+1);
	}
	lua_pop(ptLuaState, 1);
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
	rd_kafka_resp_err_t tResult;
	int iMessages;


	if( m_ptRk!=NULL )
	{
		/* Try to flush any waiting messages.
		 * Wait for a maximum of 2 seconds.
		 */
		tResult = rd_kafka_flush(m_ptRk, 2000);
		if( tResult!=RD_KAFKA_RESP_ERR_NO_ERROR )
		{
			/* Show an error. */
			iMessages = rd_kafka_outq_len(m_ptRk);
			fprintf(stderr, "RdKafkaCore(%p): failed to flush, %d messages left in the queue: %s\n", this, iMessages, rd_kafka_err2str(tResult));
		}

		rd_kafka_destroy(m_ptRk);
		rd_kafka_wait_destroyed(1000);
		m_ptRk = NULL;
	}
}



/* Set the client software name and version in a configuration.
 * Recommended here: https://github.com/edenhill/librdkafka/wiki/Language-bindings-development#reporting-client-software-name-and-version-to-broker
 * Both operations are not fatal as they are only informational.
 */
void RdKafkaCore::setClientId(rd_kafka_conf_t *ptConf)
{
	rd_kafka_conf_res_t tCfgRes;
	char acVersion[256];
	char acError[512];


	tCfgRes = rd_kafka_conf_set(ptConf, "client.software.name", "org.muhkuh.lua-kafka", acError, sizeof(acError));
	if(tCfgRes)
	{
		fprintf(stderr, "Failed to set 'client.software.name': %s\n", acError);
	}
	/* Construct the version. */
	snprintf(acVersion, sizeof(acVersion), "%s-librdkafka-%s", DIST_VERSION, rd_kafka_version_str());
	printf("RdKafkaCore(%p): Setting version to '%s'.\n", this, acVersion);
	tCfgRes = rd_kafka_conf_set(ptConf, "client.software.version", acVersion, acError, sizeof(acError));
	if(tCfgRes)
	{
		fprintf(stderr, "Failed to set 'client.software.version': %s\n", acError);
	}
}



void RdKafkaCore::createCore(const char *pcBrokerList, lua_State *ptLuaState, lua_State *ptLuaStateForConfig, int iConfigTableIndex)
{
	rd_kafka_conf_t *ptConf;
	rd_kafka_t *ptRk;
	int iResult;
	rd_kafka_conf_res_t tConfRes;
	char acError[512];


	ptConf = rd_kafka_conf_new();
	setClientId(ptConf);

	/* Set the bootstrap server as "bootstrap.servers" in the configuration. */
	tConfRes = rd_kafka_conf_set(ptConf, "bootstrap.servers", pcBrokerList, acError, sizeof(acError));
	if( tConfRes!=RD_KAFKA_CONF_OK )
	{
		rd_kafka_conf_destroy(ptConf);
		luaL_error(ptLuaState, "Failed to set the bootstrap servers: %s", acError);
	}
	else
	{
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
			rd_kafka_conf_set_error_cb(ptConf, RdKafkaCore::errorCallbackStatic);
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
	rd_kafka_topic_t *ptTopic;
	void *pvTopic;
	Topic *ptTopicObject;
	uintptr_t uiSequenceNr;


	/* Try to get the pointer to the topic object. */
	pvTopic = NULL;
	ptTopic = ptRkMessage->rkt;
	if( ptTopic!=NULL )
	{
		pvTopic = rd_kafka_topic_opaque(ptRkMessage->rkt);
	}
	if( pvTopic!=NULL )
	{
		ptTopicObject = (Topic*)pvTopic;
		ptTopicObject->onMessage(ptRkMessage);
	}
	else
	{
		/* No topic object available. Try to print something here. */
		uiSequenceNr = (uintptr_t)(ptRkMessage->_private);
		m_pvMsgOpaque = ptRkMessage->_private;
		if( ptRkMessage->err==RD_KAFKA_RESP_ERR_NO_ERROR )
		{
			printf("RdKafkaCore(%p): Message %" PRIuPTR " delivered.\n", this, uiSequenceNr);
		}
		else
		{
			++m_uiFailures;
			printf("RdKafkaCore(%p): Failed to deliver message %" PRIuPTR ": %s\n", this, uiSequenceNr, rd_kafka_err2str(ptRkMessage->err));
		}
	}
}



void RdKafkaCore::errorCallbackStatic(rd_kafka_t *ptRk, int iErr, const char *pcReason, void *pvOpaque)
{
	RdKafkaCore *ptThis;


	ptThis = (RdKafkaCore*)pvOpaque;
	ptThis->errorCallback(ptRk, iErr, pcReason);
}



void RdKafkaCore::errorCallback(rd_kafka_t *ptRk, int iErr, const char *pcReason)
{
	fprintf(stderr, "RdKafkaCore(%p): rdkafka error %d: %s\n", this, iErr, pcReason);
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
 : m_ptRk(NULL)
 , m_ptCore(NULL)
 , m_pcTopic(NULL)
 , m_ptTopic(NULL)
 , m_uiSequenceNr(0)
{
	rd_kafka_topic_conf_t *ptConf;
	int iResult;


	m_ptRk = ptCore->_getRk();

	m_pcTopic = strdup(pcTopic);

	ptConf = rd_kafka_topic_conf_new();
	/* Add the pointer to this class to the topic instance. */
	rd_kafka_topic_conf_set_opaque(ptConf, this);
	/* Load the configuration from a LUA table (if available). */
	if( ptLuaStateForConfig!=NULL )
	{
		iResult = load_topic_conf(ptLuaStateForConfig, ptConf, iConfigTableIndex);
		if( iResult!=0 )
		{
			rd_kafka_topic_conf_destroy(ptConf);
			lua_error(ptLuaState);
		}
	}

	m_ptTopic = rd_kafka_topic_new(m_ptRk, pcTopic, ptConf);
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

	if( m_pcTopic!=NULL )
	{
		free(m_pcTopic);
		m_pcTopic = NULL;
	}

	if( m_ptCore!=NULL )
	{
		m_ptCore->dereference();
	}
}



int Topic::send(int iPartition, const char *pcMessage)
{
	int iResult;
	size_t sizMessage;
	void *pvMessage;
	void *pvOpaque;
	rd_kafka_resp_err_t tError;


	/* Silently ignore NULL messages. */
	if( pcMessage==NULL )
	{
		iResult = 0;
	}
	else
	{
		/* Get the size of the message. */
		sizMessage = strlen(pcMessage);
		pvMessage = (void*)pcMessage;

		/* Use the current sequence number for the new message.
		 * Increase the sequence number counter.
		 */
		pvOpaque = (void*)(m_uiSequenceNr++);

		tError = rd_kafka_producev(
			/* Producer handle */
			m_ptRk,
			/* Topic object. */
			RD_KAFKA_V_RKT(m_ptTopic),
			/* Make a copy of the payload. */
			RD_KAFKA_V_MSGFLAGS(RD_KAFKA_MSG_F_COPY),
			/* Message value and length */
			RD_KAFKA_V_VALUE(pvMessage, sizMessage),
			/* Per-Message opaque, provided in
			 * delivery report callback as
			 * msg_opaque. */
			RD_KAFKA_V_OPAQUE(pvOpaque),
			/* End sentinel */
			RD_KAFKA_V_END
		);
		iResult = (int)tError;
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



void Topic::onMessage(const rd_kafka_message_t *ptRkMessage)
{
	rd_kafka_resp_err_t tError;
	uintptr_t uiSequenceNr;


	tError = ptRkMessage->err;
	uiSequenceNr = (uintptr_t)(ptRkMessage->_private);
	if( tError==RD_KAFKA_RESP_ERR_NO_ERROR )
	{
		printf("Topic(%p): Message %" PRIuPTR " delivered.\n", this, uiSequenceNr);
	}
	else
	{
		printf("Topic(%p): Failed to deliver message %" PRIuPTR ": %s\n", this, uiSequenceNr, rd_kafka_err2str(tError));
	}
}



const char *Topic::error2string(int iError)
{
	rd_kafka_resp_err_t tError;


	tError = (rd_kafka_resp_err_t)iError;
	return rd_kafka_err2str(tError);
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
