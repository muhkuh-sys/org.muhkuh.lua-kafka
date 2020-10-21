%module kafka

%include "stdint.i"
%include "typemaps.i"

%{
	#include "wrapper.h"
%}


/* This typemap adds "SWIGTYPE_" to the name of the input parameter to
 * construct the swig typename. The parameter name must match the definition
 * in the wrapper.
 */
%typemap(in, numinputs=0) swig_type_info *
%{
	$1 = SWIGTYPE_$1_name;
%}


/* This typemap passes the LUA state to the function. */
%typemap(in, numinputs=0) (lua_State *MUHKUH_LUA_STATE)
%{
	$1 = L;
%}


/* This typemap passes Lua state to the function. The function must create one
 * lua object on the stack. This is passes as the return value to lua.
 * No further checks are done!
 */
%typemap(in, numinputs=0) lua_State *MUHKUH_SWIG_OUTPUT_CUSTOM_OBJECT
%{
	$1 = L;
	++SWIG_arg;
%}


%typemap(in) (const char *pcBUFFER_IN, size_t sizBUFFER_IN)
{
        size_t sizBuffer;
        $1 = (char*)lua_tolstring(L, $argnum, &sizBuffer);
        $2 = sizBuffer;
}


%typemap(in, numinputs=0) (char **ppcBUFFER_OUT, size_t *psizBUFFER_OUT)
%{
	char *pcOutputData;
	size_t sizOutputData;
	$1 = &pcOutputData;
	$2 = &sizOutputData;
%}

/* NOTE: This "argout" typemap can only be used in combination with the above "in" typemap. */
%typemap(argout) (char **ppcBUFFER_OUT, size_t *psizBUFFER_OUT)
%{
	if( pcOutputData!=NULL && sizOutputData!=0 )
	{
		lua_pushlstring(L, pcOutputData, sizOutputData);
		free(pcOutputData);
	}
	else
	{
		lua_pushnil(L);
	}
	++SWIG_arg;
%}


%typemap(in, numinputs=0) (unsigned int *puiUINT_OUT)
%{
	unsigned int uiOut;
	$1 = &uiOut;
%}
%typemap(argout) (unsigned int *puiUINT_OUT)
%{
#if LUA_VERSION_NUM>=504
	lua_pushinteger(L, uiOut);
#else
	lua_pushnumber(L, uiOut);
#endif
	++SWIG_arg;
%}


%typemap(in, numinputs=0) (uintptr_t *puiUINT_OR_NIL)
%{
	uintptr_t uiUIntOrNil;
	$1 = &uiUIntOrNil;
%}
%typemap(argout) (unsigned int *puiUINT_OR_NIL)
%{
	if( uiUIntOrNil!=0 )
	{
#if LUA_VERSION_NUM>=504
		lua_pushinteger(L, uiUIntOrNil);
#else
		lua_pushnumber(L, uiUIntOrNil);
#endif
	}
	else
	{
		lua_pushnil(L);
	}
	++SWIG_arg;
%}


%typemap(out) RESULT_UINT
%{
#if LUA_VERSION_NUM>=504
	lua_pushinteger(L, $1);
#else
	lua_pushnumber(L, $1);
#endif
	++SWIG_arg;
%}


/* This typemap expects a table as input and replaces it with the Lua state.
 * This allows the function to add elements to the table without the overhead
 * of creating and deleting a C array.
 */
%typemap(in,checkfn="lua_istable") lua_State *ptLuaStateForTableAccess
%{
        $1 = L;
%}
%typemap(default) (lua_State *ptLuaStateForTableAccessOptional) {
        $1 = NULL;
}
%typemap(in,checkfn="lua_istable") (lua_State *ptLuaStateForTableAccessOptional)
%{
        $1 = L;
%}



/* The "create_topic" method of the "Producer" object returns a new "Topic" object. It must be freed by the LUA interpreter. */
%newobject Producer::create_topic;


%include "wrapper.h"
