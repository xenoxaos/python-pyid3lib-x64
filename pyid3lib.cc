#include <Python.h>

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

#include <id3/globals.h>
#include <id3/field.h>
#include <id3/id3lib_frame.h>
#include <id3/tag.h>

#if PY_VERSION_HEX < 0x02050000 && !defined(PY_SSIZE_T_MIN)
typedef int Py_ssize_t;
#define PY_SSIZE_T_MAX INT_MAX
#define PY_SSIZE_T_MIN INT_MIN
#endif

typedef struct
{
    PyObject_HEAD

    ID3_Tag* tag;
    ID3_Frame** frames;
    Py_ssize_t size, alloc;
} ID3Object;

typedef struct
{
    PyObject_HEAD

    ID3Object* id3_obj;
    int tagiter_done;
    int pos, size;
} ID3IterObject;

#define MODULE_NAME      "pyid3lib"
#define MODULE_VERSION   "0.5.1"

#define COPYRIGHT_NOTICE "Copyright (c) 2002-3 Doug Zongker.  All rights reserved."

using namespace std;

#ifndef FUNC_PY_STATIC
#define FUNC_PY_STATIC(fn)  static PyObject* fn( PyObject* self, PyObject* args )
#endif

PyObject* ID3Error;

void initi3d( void );
static PyObject* id3_new( PyObject* self, PyObject* args );
static void id3_dealloc( ID3Object* self );
static PyObject* id3_getattr( ID3Object* self, char* attrname );
static int id3_setattr( ID3Object* self, char* attrname, PyObject* val );

static PyObject* id3_update( ID3Object* self );

static PyObject* id3_iter( ID3Object* self );
static void id3iter_dealloc( ID3IterObject* self );
static PyObject* id3iter_getiter( PyObject* self );
static PyObject* id3iter_iternext( ID3IterObject* self );

static PyObject* frame_id_key_obj;
static PyObject* field_keys[ID3FN_LASTFIELDID+1];

static PyObject* dict_from_frame( ID3_Frame* frame );
static ID3_Frame* frame_from_dict( PyObject* dict );
static ID3_Frame* frame_from_dict( ID3_FrameID fid, PyObject* dict );

static int id3_length( ID3Object* self );
static PyObject* id3_item( ID3Object* self, int index );
static PyObject* id3_slice( ID3Object* self, int start, int end );
static int id3_ass_item( ID3Object* self, int index, PyObject* dict );
static int id3_ass_slice( ID3Object* self, int start, int end, PyObject* dict );
static int id3_contains( ID3Object* self, PyObject* other );

static PyObject* id3_append( ID3Object* self, PyObject* args );
static PyObject* id3_extend( ID3Object* self, PyObject* args );
static PyObject* id3_count( ID3Object* self, PyObject* args );
static PyObject* id3_index( ID3Object* self, PyObject* args );
static PyObject* id3_insert( ID3Object* self, PyObject* args );
static PyObject* id3_pop( ID3Object* self, PyObject* args );
static PyObject* id3_remove( ID3Object* self, PyObject* args );

static PyObject* frameid_lookup = NULL;


static PySequenceMethods tag_as_sequence = {
#if PY_VERSION_HEX >= 0x02050000
    (lenfunc)id3_length,
#else
    (inquiry)id3_length,
#endif
    NULL,
    NULL,
#if PY_VERSION_HEX >= 0x02050000
    (ssizeargfunc)id3_item,
    (ssizessizeargfunc)id3_slice,
    (ssizeobjargproc)id3_ass_item,
    (ssizessizeobjargproc)id3_ass_slice,
#else
    (intargfunc)id3_item,
    (intintargfunc)id3_slice,
    (intobjargproc)id3_ass_item,
    (intintobjargproc)id3_ass_slice,
#endif
    (objobjproc)id3_contains,
    NULL,
    NULL,
};

static PyMethodDef id3_methods[] = {
    { "update", (PyCFunction)id3_update, METH_NOARGS },

    // standard sequence methods
    { "append", (PyCFunction)id3_append, METH_VARARGS },
    { "extend", (PyCFunction)id3_extend, METH_VARARGS },
    { "count", (PyCFunction)id3_count, METH_VARARGS },
    { "index", (PyCFunction)id3_index, METH_VARARGS },
    { "insert", (PyCFunction)id3_insert, METH_VARARGS },
    { "pop", (PyCFunction)id3_pop, METH_VARARGS },
    { "remove", (PyCFunction)id3_remove, METH_VARARGS },
    { NULL, NULL }
};
    

PyTypeObject ID3Type = {
    PyObject_HEAD_INIT(&PyType_Type)
    0,
    MODULE_NAME ".tag",
    sizeof( ID3Object ),
    0,
    (destructor)id3_dealloc,           // tp_dealloc
    0,                                 // tp_print
    (getattrfunc)id3_getattr,          // tp_getattr
    (setattrfunc)id3_setattr,          // tp_setattr
    0,                                 // tp_compare
    0,                                 // tp_repr
    0,                                 // tp_as_number
    &tag_as_sequence,                  // tp_as_sequence
    0,                                 // tp_as_mapping
    0,                                 // tp_hash
    0,                                 // tp_call 
    0,                                 // tp_str 
    0,                                 // tp_getattro 
    0,                                 // tp_setattro 
    0,                                 // tp_as_buffer 
    Py_TPFLAGS_DEFAULT,                // tp_flags 
    0,                                 // tp_doc 
    0,                                 // tp_traverse 
    0,                                 // tp_clear 
    0,                                 // tp_richcompare 
    0,                                 // tp_weaklistoffset 
    (getiterfunc)id3_iter,             // tp_iter 
    0,                                 // tp_iternext
    id3_methods,
};

static PyMethodDef id3iter_methods[] = {
    { "next", (PyCFunction)id3iter_iternext, METH_NOARGS },
    { NULL, NULL }
};

PyTypeObject ID3IterType = {
    PyObject_HEAD_INIT(&PyType_Type)
    0,
    MODULE_NAME ".tag-iterator",
    sizeof( ID3IterObject ),
    0,
    (destructor)id3iter_dealloc,       // tp_dealloc
    0,                                 // tp_print
    0,                                 // tp_getattr
    0,                                 // tp_setattr
    0,                                 // tp_compare
    0,                                 // tp_repr
    0,                                 // tp_as_number
    0,                                 // tp_as_sequence
    0,                                 // tp_as_mapping
    0,                                 // tp_hash
    0,                                 // tp_call 
    0,                                 // tp_str 
    PyObject_GenericGetAttr,           // tp_getattro 
    0,                                 // tp_setattro 
    0,                                 // tp_as_buffer 
    Py_TPFLAGS_DEFAULT,                // tp_flags 
    0,                                 // tp_doc 
    0,                                 // tp_traverse 
    0,                                 // tp_clear 
    0,                                 // tp_richcompare 
    0,                                 // tp_weaklistoffset 
    (getiterfunc)id3iter_getiter,      // tp_iter 
    (iternextfunc)id3iter_iternext,    // tp_iternext 
    id3iter_methods, 		       // tp_methods 
    0, 				       // tp_members 
    0, 				       // tp_getset 
    0, 				       // tp_base 
    0, 				       // tp_dict 
    0, 				       // tp_descr_get 
    0, 				       // tp_descr_set 
};

static PyObject* id3_iter( ID3Object* self )
{
    ID3IterObject* it;

    it = PyObject_New( ID3IterObject, &ID3IterType );
    if ( it == NULL )
	return NULL;
    Py_INCREF( self );
    it->id3_obj = self;
    it->tagiter_done = 0;
    it->pos = 0;
    it->size = self->size;

    return (PyObject*)it;
}

static void id3iter_dealloc( ID3IterObject* self )
{
    Py_DECREF( self->id3_obj );
    PyObject_DEL( self );
}

static PyObject* id3iter_getiter( PyObject* self )
{
    Py_INCREF( self );
    return self;
}

static PyObject* id3iter_iternext( ID3IterObject* self )
{
    if ( self->tagiter_done )
	return NULL;

    if ( self->size != self->id3_obj->size )
    {
	PyErr_SetString( PyExc_RuntimeError, "nunber of frames changed during iteration" );
	return NULL;
    }

    if ( self->pos >= self->size )
    {
	self->tagiter_done = 1;
	return NULL;
    }

    return dict_from_frame( self->id3_obj->frames[self->pos++] );
}

/////////////////
//
//   tp_as_sequence methods
//
/////////////////

static int id3_length( ID3Object* self )
{
    return self->size;
}

static PyObject* id3_item( ID3Object* self, int index )
{
    if ( index < 0 )
	index += self->size;

    if ( index < 0 || index >= self->size )
    {
	PyErr_SetString( PyExc_IndexError, "frame index out of range" );
	return NULL;
    }

    return dict_from_frame( self->frames[index] );
}

static PyObject* id3_slice( ID3Object* self, int start, int end )
{
    PyObject* result;
    int i;

    if ( start < 0 )
	start = 0;
    else if ( start > self->size )
	start = self->size;

    if ( end < start )
	end = start;
    else if ( end > self->size )
	end = self->size;

    result = PyList_New( end-start );
    if ( result == NULL )
	return NULL;

    for ( i = start; i < end; ++i )
    {
	PyObject* v = dict_from_frame( self->frames[i] );
	PyList_SetItem( result, i-start, v );
    }

    return result;
}


static int id3_ass_item( ID3Object* self, int index, PyObject* dict )
{
    ID3_Frame* newframe;

    if ( index < 0 )
	index += self->size;
    if ( index < 0 || index >= self->size )
    {
	PyErr_SetString( PyExc_IndexError, "frame assignment index out of range" );
	return -1;
    }

    if ( dict == NULL )
    {
	// deleting a frame
	
	int i;
	
	delete self->frames[index];

	for ( i = index+1; i < self->size; ++i )
	    self->frames[i-1] = self->frames[i];
	--self->size;

	return 0;
    }
    
    if ( !PyDict_Check( dict ) )
    {
	PyErr_SetString( ID3Error, "frame assignment must be from dictionary" );
	return -1;
    }
    
    newframe = frame_from_dict( dict );
    if ( newframe == NULL )
	return -1;

    delete self->frames[index];
    self->frames[index] = newframe;

    return 0;
}

static ID3_Frame** frames_from_dictseq( PyObject* dictseq, int *count )
{
    ID3_Frame** newframes;
    PyObject* dict;
    int i, n;
    
    if ( !PySequence_Check( dictseq ) )
    {
	PyErr_SetString( ID3Error, "slice assignment must be from sequence of dictionaries" );
	*count = -1;
	return NULL;
    }

    n = PySequence_Size( dictseq );
    if ( n == 0 )
    {
	*count = 0;
	return NULL;
    }
    
    newframes = new ID3_Frame* [n];
    for ( i = 0; i < n; ++i )
	newframes[i] = NULL;
	
    for ( i = 0; i < n; ++i )
    {
	dict = PySequence_GetItem( dictseq, i );
	if ( !PyDict_Check( dict ) )
	{
	    PyErr_SetString( ID3Error, "slice assignment must be from sequence of dictionaries" );
	    Py_DECREF( dict );
	    goto abort;
	}

	newframes[i] = frame_from_dict( dict );
	Py_DECREF( dict );
	if ( newframes[i] == NULL )
	    goto abort;
    }

    *count = n;
    return newframes;
    
    
    abort:
    
    for( i = 0; i < n; ++i )
	if ( newframes[i] )
	    delete newframes[i];
    delete [] newframes;

    *count = -1;
    return NULL;
}

static int id3_ass_slice( ID3Object* self, int start, int end, PyObject* dictseq )
{
    int i, n;
    int newsize;
    ID3_Frame** newframes;
	
    if ( start < 0 )
	start = 0;
    else if ( start > self->size )
	start = self->size;

    if ( end < start )
	end = start;
    else if ( end > self->size )
	end = self->size;

    if ( dictseq == NULL )
    {
    deleteslice:
	for ( i = start; i < end; ++i )
	    delete self->frames[i];

	for ( i = end; i < self->size; ++i )
	    self->frames[i-end+start] = self->frames[i];
	self->size -= (end-start);

	return 0;
    }

    // first, try to create frames from dictseq

    newframes = frames_from_dictseq( dictseq, &n );
    if ( newframes == NULL )
    {
	if ( n == 0 )
	    goto deleteslice;    // sequence was empty
	else
	    return -1;           // some error occurred in reading dictseq
    }
    
    // hooray, no problems with the caller's value.  start shifting
    // around existing frames to insert the new ones.
    
    newsize = self->size - (end-start) + n;

    if ( newsize > self->alloc )
    {
	self->alloc = newsize;
	self->frames = (ID3_Frame**)realloc( self->frames,
					     self->alloc * sizeof( ID3_Frame* ) );
    }

    if ( newsize >= self->size )
    {
	// shift frames after "end" to the right
	for ( i = self->size-1; i >= end; --i )
	    self->frames[i + n - end + start] = self->frames[i];
    }
    else
    {
	// shift frames after "end" to the left
	for ( i = end; i < self->size; ++i )
	    self->frames[i + n - end + start] = self->frames[i];
    }
    for ( i = 0; i < n; ++i )
	self->frames[start + i] = newframes[i];
    delete [] newframes;
    self->size = newsize;

    return 0;
}

/////////////////
//
//   explicit mutable sequence methods
//
/////////////////

static int id3_contains( ID3Object* self, PyObject* other )
{
    int i;
    
    if ( !PyString_Check( other ) )
    {
	PyErr_SetString( ID3Error, "'in <tag>' requires string as left operand" );
	return -1;
    }

    PyObject* tuple;
    tuple = PyDict_GetItem( frameid_lookup, other );
    if ( tuple == NULL )
    {
	PyErr_Format( ID3Error, "frame id '%s' not supported by id3lib",
		      PyString_AsString( other ) );
	return -1;
    }

    ID3_FrameID fid = (ID3_FrameID)PyInt_AsLong( PyTuple_GetItem( tuple, 0 ) );

    for ( i = 0; i < self->size; ++i )
	if ( self->frames[i]->GetID() == fid )
	    return 1;

    return 0;
}

static PyObject* id3_append( ID3Object* self, PyObject* args )
{
    PyObject* dict;

    if ( !PyArg_ParseTuple( args, "O", &dict ) )
	return NULL;
    Py_INCREF( dict );

    if ( !PyDict_Check( dict ) )
    {
	PyErr_SetString( ID3Error, "frame append must be from dictionary" );
	Py_DECREF( dict );
	return NULL;
    }
    
    ID3_Frame* newframe = frame_from_dict( dict );
    Py_DECREF( dict );
    if ( newframe == NULL )
	return NULL;

    if ( self->size + 1 > self->alloc )
    {
	self->alloc += 8;
	self->frames = (ID3_Frame**)realloc( self->frames, self->alloc * sizeof( ID3_Frame* ) );
    }

    self->frames[self->size++] = newframe;

    Py_INCREF( Py_None );
    return Py_None;
}

static PyObject* id3_extend( ID3Object* self, PyObject* args )
{
    PyObject* dictseq;
    int i, n;
    ID3_Frame** newframes;

    if ( !PyArg_ParseTuple( args, "O", &dictseq ) )
	return NULL;
    Py_INCREF( dictseq );

    newframes = frames_from_dictseq( dictseq, &n );
    Py_DECREF( dictseq );

    if ( newframes == NULL )
    {
	if ( n == 0 )
	    goto done;    // sequence was empty
	else
	    return NULL;  // error processing dictseq
    }

    if ( self->size + n > self->alloc )
    {
	self->alloc += n;
	self->frames = (ID3_Frame**)realloc( self->frames, self->alloc * sizeof( ID3_Frame* ) );
    }

    for ( i = 0; i < n; ++i )
	self->frames[self->size + i] = newframes[i];
    self->size += n;
    delete [] newframes;

 done:
    Py_INCREF( Py_None );
    return Py_None;
    
}

static PyObject* id3_count( ID3Object* self, PyObject* args )
{
    int i, c;
    PyObject* other;

    if ( !PyArg_ParseTuple( args, "O!", &PyString_Type, &other ) )
	return NULL;
    Py_INCREF( other );

    PyObject* tuple;
    tuple = PyDict_GetItem( frameid_lookup, other );
    Py_DECREF( other );
    if ( tuple == NULL )
    {
	PyErr_Format( ID3Error, "frame id '%s' not supported by id3lib",
		      PyString_AsString( other ) );
	return NULL;
    }

    ID3_FrameID fid = (ID3_FrameID)PyInt_AsLong( PyTuple_GetItem( tuple, 0 ) );

    c = 0;
    for ( i = 0; i < self->size; ++i )
	if ( self->frames[i]->GetID() == fid )
	    ++c;

    return PyInt_FromLong( c );
}

static PyObject* id3_index( ID3Object* self, PyObject* args )
{
    int i;
    PyObject* other;

    if ( !PyArg_ParseTuple( args, "O!", &PyString_Type, &other ) )
	return NULL;
    Py_INCREF( other );
    
    PyObject* tuple;
    tuple = PyDict_GetItem( frameid_lookup, other );
    Py_DECREF( other );
    if ( tuple == NULL )
    {
	PyErr_Format( ID3Error, "frame id '%s' not supported by id3lib",
		      PyString_AsString( other ) );
	return NULL;
    }

    ID3_FrameID fid = (ID3_FrameID)PyInt_AsLong( PyTuple_GetItem( tuple, 0 ) );

    for ( i = 0; i < self->size; ++i )
	if ( self->frames[i]->GetID() == fid )
	    return PyInt_FromLong( i );

    PyErr_SetString( PyExc_ValueError, "frame id not in tag" );
    return NULL;
}

static PyObject* id3_insert( ID3Object* self, PyObject* args )
{
    PyObject* dict;
    int i, index;

    if ( !PyArg_ParseTuple( args, "iO", &index, &dict ) )
	return NULL;
    Py_INCREF( dict );

    if ( !PyDict_Check( dict ) )
    {
	PyErr_SetString( ID3Error, "frame insert must be from dictionary" );
	Py_DECREF( dict );
	return NULL;
    }

    ID3_Frame* newframe = frame_from_dict( dict );
    Py_DECREF( dict );
    if ( newframe == NULL )
	return NULL;

    if ( self->size + 1 > self->alloc )
    {
	self->alloc += 8;
	self->frames = (ID3_Frame**)realloc( self->frames, self->alloc * sizeof( ID3_Frame* ) );
    }

    if ( index < 0 )
	index = 0;
    else if ( index > self->size )
	index = self->size;
    
    for ( i = self->size-1; i >= index; --i )
	self->frames[i+1] = self->frames[i];
    self->frames[index] = newframe;
    ++self->size;

    Py_INCREF( Py_None );
    return Py_None;
}

static PyObject* id3_pop( ID3Object* self, PyObject* args )
{
    int index = self->size-1;
    int i;
    PyObject* result;

    if ( !PyArg_ParseTuple( args, "|i", &index ) )
	return NULL;

    if ( self->size == 0 )
    {
	PyErr_SetString( PyExc_IndexError, "pop from empty tag" );
	return NULL;
    }
    
    if ( index < 0 || index >= self->size )
    {
	PyErr_SetString( PyExc_IndexError, "pop index out of range" );
	return NULL;
    }

    result = dict_from_frame( self->frames[index] );

    delete self->frames[index];
    for ( i = index+1; i < self->size; ++i )
	self->frames[i-1] = self->frames[i];
    --self->size;

    return result;
}

static PyObject* id3_remove( ID3Object* self, PyObject* args )
{
    int i, index;
    PyObject* other;
    PyObject* result;

    if ( !PyArg_ParseTuple( args, "O!", &PyString_Type, &other ) )
	return NULL;
    Py_INCREF( other );
    
    PyObject* tuple;
    tuple = PyDict_GetItem( frameid_lookup, other );
    Py_DECREF( other );
    if ( tuple == NULL )
    {
	PyErr_Format( ID3Error, "frame id '%s' not supported by id3lib",
		      PyString_AsString( other ) );
	return NULL;
    }

    ID3_FrameID fid = (ID3_FrameID)PyInt_AsLong( PyTuple_GetItem( tuple, 0 ) );

    index = -1;
    for ( i = 0; i < self->size; ++i )
	if ( self->frames[i]->GetID() == fid )
	{
	    index = i;
	    break;
	}

    if ( index == -1 )
    {
	PyErr_SetString( PyExc_ValueError, "frame id not in tag" );
	return NULL;
    }

    result = dict_from_frame( self->frames[index] );

    delete self->frames[index];
    for ( i = index+1; i < self->size; ++i )
	self->frames[i-1] = self->frames[i];
    --self->size;

    return result;
}



/////////////////
//
//   frames <--> dictionaries
//
/////////////////

static PyObject* dict_from_frame( ID3_Frame* frame )
{
    ID3_FrameID fid;
    ID3_FrameInfo finfo;
    PyObject* result;
    PyObject* item;
    
    fid = frame->GetID();
    
    result = PyDict_New();

    item = PyString_FromString( finfo.LongName( fid ) );
    PyDict_SetItem( result, frame_id_key_obj, item );
    Py_DECREF( item );
    
    ID3_Frame::Iterator* fiter = frame->CreateIterator();
    ID3_Field* field;
    ID3_TextEnc enc;
    while( (field = fiter->GetNext()) )
    {
	ID3_FieldID flid = field->GetID();
	if ( field_keys[flid] == NULL )
	    continue;
	
	switch( field->GetType() )
	{
	  case ID3FTY_TEXTSTRING:
	    enc = field->GetEncoding();
	    field->SetEncoding( ID3TE_ASCII );
	    item = PyString_FromString( field->GetRawText() );
	    field->SetEncoding( ID3TE_ASCII );
	    break;

	  case ID3FTY_INTEGER:
	    item = PyInt_FromLong( field->Get() );
	    break;
	    
	  case ID3FTY_BINARY:
	    int size;
	    size = field->Size();
	    item = PyString_FromStringAndSize( (char*)(field->GetRawBinary()), size );
	    break;
	}

	PyDict_SetItem( result, field_keys[flid], item );
	Py_DECREF( item );
	    
    }
    delete fiter;

    return result;
}

static ID3_Frame* frame_from_dict( PyObject* dict )
{
    PyObject* id = PyDict_GetItemString( dict, "frameid" );
    if ( id == NULL || !PyString_Check( id ) )
    {
	PyErr_SetString( ID3Error, "dictionary must contain 'frameid' with string value" );
	return NULL;
    }

    PyObject* tuple;

    tuple = PyDict_GetItem( frameid_lookup, id );
    if ( tuple == NULL )
    {
	PyErr_Format( ID3Error, "frame id '%s' not supported by id3lib",
		      PyString_AsString( id ) );
	return NULL;
    }

    ID3_FrameID fid = (ID3_FrameID)PyInt_AsLong( PyTuple_GetItem( tuple, 0 ) );

    return frame_from_dict( fid, dict );
}

    

static ID3_Frame* frame_from_dict( ID3_FrameID fid, PyObject* dict )
{
    char* data;
    Py_ssize_t size;
    
    ID3_Field* field;
    ID3_FieldID flid;
    
    ID3_Frame* frame = new ID3_Frame( fid );

    ID3_Frame::Iterator* fiter = frame->CreateIterator();
    while( (field = fiter->GetNext()) )
    {
	flid = field->GetID();
	PyObject* item;

	if ( field_keys[flid] == NULL )
	    continue;

	item = PyDict_GetItem( dict, field_keys[flid] );
	if ( item == NULL )
	    continue;
	
	switch( field->GetType() )
	{
	  case ID3FTY_TEXTSTRING:
	    if ( !PyString_Check( item ) )
	    {
		PyErr_Format( ID3Error, "bad dictionary: '%s' value must be string", PyString_AsString( field_keys[flid] ) );
		delete fiter;
		delete frame;
		return NULL;
	    }
	    field->SetEncoding( ID3TE_ISO8859_1 );
	    field->Set( PyString_AsString( item ) );
	    break;
	    
	  case ID3FTY_INTEGER:
	    if ( !PyInt_Check( item ) )
	    {
		PyErr_Format( ID3Error, "bad dictionary: '%s' value must be int", PyString_AsString( field_keys[flid] ) );
		delete fiter;
		delete frame;
		return NULL;
	    }
	    field->Set( PyInt_AsLong( item ) );
	    break;
	    
	  case ID3FTY_BINARY:
	    if ( !PyString_Check( item ) )
	    {
		PyErr_Format( ID3Error, "bad dictionary: '%s' value must be data string", PyString_AsString( field_keys[flid] ) );
		delete fiter;
		delete frame;
		return NULL;
	    }
	    PyString_AsStringAndSize( item, &data, &size );
	    field->Set( (unsigned char*)data, size );
	    break;
	}
    }
    delete fiter;

    return frame;
}

/////////////////////
//
//  accessing frames via "magic attributes"
//
/////////////////////

enum frame_type
{
    PYFD_Text,
    PYFD_Tracknum,
    PYFD_Year,
    PYFD_URL,
};

typedef struct
{
    char* name;
    ID3_FrameID fid;
    frame_type type;
} magic_attribute;

static int magic_attribute_table_size = -1;

// this list must be sorted by the "name" field.
static magic_attribute magic_attribute_table[] = {
    { "album",              ID3FID_ALBUM,                PYFD_Text },
    { "artist",             ID3FID_LEADARTIST,           PYFD_Text },    // synonym
    { "band",               ID3FID_BAND,                 PYFD_Text },
    { "bpm",                ID3FID_BPM,                  PYFD_Text },
    { "composer",           ID3FID_COMPOSER,             PYFD_Text },
    { "conductor",          ID3FID_CONDUCTOR,            PYFD_Text },
    { "contentgroup",       ID3FID_CONTENTGROUP,         PYFD_Text },
    { "contenttype",        ID3FID_CONTENTTYPE,          PYFD_Text },
    { "copyright",          ID3FID_COPYRIGHT,            PYFD_Text },
    { "date",               ID3FID_DATE,                 PYFD_Text },
    { "encodedby",          ID3FID_ENCODEDBY,            PYFD_Text },
    { "encodersettings",    ID3FID_ENCODERSETTINGS,      PYFD_Text },
    { "fileowner",          ID3FID_FILEOWNER,            PYFD_Text },
    { "filetype",           ID3FID_FILETYPE,             PYFD_Text },
    { "initialkey",         ID3FID_INITIALKEY,           PYFD_Text },
    { "involvedpeople",     ID3FID_INVOLVEDPEOPLE,       PYFD_Text },
    { "isrc",               ID3FID_ISRC,                 PYFD_Text },
    { "language",           ID3FID_LANGUAGE,             PYFD_Text },
    { "leadartist",         ID3FID_LEADARTIST,           PYFD_Text },
    { "lyricist",           ID3FID_LYRICIST,             PYFD_Text },
    { "mediatype",          ID3FID_MEDIATYPE,            PYFD_Text },
    { "mixartist",          ID3FID_MIXARTIST,            PYFD_Text },
    { "netradioowner",      ID3FID_NETRADIOOWNER,        PYFD_Text },
    { "netradiostation",    ID3FID_NETRADIOSTATION,      PYFD_Text },
    { "origalbum",          ID3FID_ORIGALBUM,            PYFD_Text },
    { "origartist",         ID3FID_ORIGARTIST,           PYFD_Text },
    { "origfilename",       ID3FID_ORIGFILENAME,         PYFD_Text },
    { "origlyricist",       ID3FID_ORIGLYRICIST,         PYFD_Text },
    { "origyear",           ID3FID_ORIGYEAR,             PYFD_Text },
    { "partinset",          ID3FID_PARTINSET,            PYFD_Tracknum },
    { "playlistdelay",      ID3FID_PLAYLISTDELAY,        PYFD_Text },
    { "publisher",          ID3FID_PUBLISHER,            PYFD_Text },
    { "recordingdates",     ID3FID_RECORDINGDATES,       PYFD_Text },
    { "size",               ID3FID_SIZE,                 PYFD_Text },
    { "songlen",            ID3FID_SONGLEN,              PYFD_Text },
    { "subtitle",           ID3FID_SUBTITLE,             PYFD_Text },
    { "time",               ID3FID_TIME,                 PYFD_Text },
    { "title",              ID3FID_TITLE,                PYFD_Text },
    { "track",              ID3FID_TRACKNUM,             PYFD_Tracknum },   // synonym
    { "tracknum",           ID3FID_TRACKNUM,             PYFD_Tracknum },
    { "wwwartist",          ID3FID_WWWARTIST,            PYFD_URL },
    { "wwwaudiofile",       ID3FID_WWWAUDIOFILE,         PYFD_URL },
    { "wwwaudiosource",     ID3FID_WWWAUDIOSOURCE,       PYFD_URL },
    { "wwwcommercialinfo",  ID3FID_WWWCOMMERCIALINFO,    PYFD_URL },
    { "wwwcopyright",       ID3FID_WWWCOPYRIGHT,         PYFD_URL },
    { "wwwpayment",         ID3FID_WWWPAYMENT,           PYFD_URL },
    { "wwwpublisher",       ID3FID_WWWPUBLISHER,         PYFD_URL },
    { "wwwradiopage",       ID3FID_WWWRADIOPAGE,         PYFD_URL },
    { "year",               ID3FID_YEAR,                 PYFD_Year },
    { NULL },
};    

static int magic_attribute_compare( const void* a, const void *b )
{
    return strcmp( (char*)a,
                   ((magic_attribute*)b)->name );
}

static PyObject* id3_getattr( ID3Object* self, char* attrname )
{
    PyObject* result = NULL;
    magic_attribute* p;
    int i, n;

    if ( strcmp( attrname, "__members__" ) == 0 )
    {
        static PyObject* memberlist = NULL;
        PyObject* temp;

        if ( memberlist == NULL )
        {
            // build the memberlist once, and hold on to it.
            
            memberlist = PyList_New( 0 );
            for ( i = 0; i < magic_attribute_table_size; ++i )
                PyList_Append( memberlist, PyString_FromString( magic_attribute_table[i].name ) );
            PyList_Append( memberlist, PyString_FromString( "track" ) );
        }

        // make a copy of the memberlist to return
        
        n = PyList_Size( memberlist );
        result = PyList_New( n );
        for ( i = 0; i < n; ++i )
        {
            temp = PyList_GET_ITEM( memberlist, i );
            Py_INCREF( temp );
            PyList_SET_ITEM( result, i, temp );
        }
            
        return result;
    }
    
    if ( (p = (magic_attribute*)bsearch( attrname,
					 magic_attribute_table,
					 magic_attribute_table_size,
					 sizeof( magic_attribute ),
					 magic_attribute_compare )) )
    {
        ID3_Frame* frame;
	const char* str;
	char* slash;
        ID3_Field* fld;
	int i;

	frame = NULL;
	for ( i = 0; i < self->size; ++i )
	    if ( self->frames[i]->GetID() == p->fid )
	    {
		frame = self->frames[i];
		break;
	    }

        if ( frame == NULL )
        {
	    PyErr_Format( PyExc_AttributeError, "tag has no '%s' frame", attrname );
	    result = NULL;
	    goto done;
        }
        
        switch( p->type )
        {
          case PYFD_Text:
            fld = frame->GetField( ID3FN_TEXT );
            n = fld->Size();
            result = PyString_FromStringAndSize( fld->GetRawText(), n );
            break;

          case PYFD_URL:
            fld = frame->GetField( ID3FN_URL );
            n = fld->Size();
            result = PyString_FromStringAndSize( fld->GetRawText(), n );
            break;
	    
          case PYFD_Year:
	    result = PyInt_FromLong( atoi( frame->GetField( ID3FN_TEXT )->GetRawText() ) );
	    break;

          case PYFD_Tracknum:
            fld = frame->GetField( ID3FN_TEXT );
            str = fld->GetRawText();
            
            if ( (slash = strchr( (char*)str, '/' )) != NULL )
                result = Py_BuildValue( "ii", atoi( str ), atoi( slash+1 ) );
            else
                result = Py_BuildValue( "(i)", atoi( str ) );
            break;
        }
    }
    else
        result = Py_FindMethod( id3_methods, (PyObject*)self, attrname );

done:
    return result;
}

static int id3_setattr( ID3Object* self, char* attrname, PyObject* val )
{
    ID3_Frame* newframe;
    ID3_Field* field;
    magic_attribute* p;

    if ( (p = (magic_attribute*)bsearch( attrname,
					 magic_attribute_table,
					 magic_attribute_table_size,
					 sizeof( magic_attribute ),
					 magic_attribute_compare )) )
    {
	int i, j;
	
	// for "del x.attr" or "x.attr = None", just delete all frames
	// of the appropriate type.
        if ( val == NULL || val == Py_None )
        {
	    j = 0;
	    for ( i = 0; i < self->size; ++i )
	    {
		if ( self->frames[i]->GetID() == p->fid )
		    delete self->frames[i];
		else
		    self->frames[j++] = self->frames[i];
	    }
	    self->size = j;
	    
            return 0;
        }

	newframe = NULL;
	
	switch( p->type )
	{
	  case PYFD_Text:
	    if ( !PyString_Check( val ) )
	    {
		PyErr_Format( ID3Error, "'%s' attribute must be string", attrname );
		return -1;
	    }
		
	    newframe = new ID3_Frame( p->fid );
	    newframe->GetField( ID3FN_TEXT )->Set( PyString_AsString( val ) );
	    break;
		
	  case PYFD_URL:
	    if ( !PyString_Check( val ) )
	    {
		PyErr_Format( ID3Error, "'%s' attribute must be string", attrname );
		return -1;
	    }

	    newframe = new ID3_Frame( p->fid );
	    newframe->GetField( ID3FN_URL )->Set( PyString_AsString( val ) );
	    break;

	  case PYFD_Year:
	    newframe = new ID3_Frame( p->fid );
	    field = newframe->GetField( ID3FN_TEXT );
		
	    if ( PyString_Check( val ) )
		field->Set( PyString_AsString( val ) );
	    else if ( PyInt_Check( val ) )
	    {
		char buffer[20];
		sprintf( buffer, "%04ld", PyInt_AsLong( val ) );
		field->Set( buffer );
	    }
	    else
	    {
		delete newframe;
		PyErr_Format( ID3Error, "'%s' attribute must be string or int", attrname );
		return -1;
	    }
	    break;

	  case PYFD_Tracknum:
	    newframe = new ID3_Frame( p->fid );
	    field = newframe->GetField( ID3FN_TEXT );
		
	    if ( PyString_Check( val ) )
		field->Set( PyString_AsString( val ) );
	    else if ( PyInt_Check( val ) )
	    {
		char buffer[20];
		sprintf( buffer, "%ld", PyInt_AsLong( val ) );
		field->Set( buffer );
	    }
	    else if ( PyTuple_Check( val ) )
	    {
		if ( PyTuple_Size( val ) == 1 &&
		     PyInt_Check( PyTuple_GetItem( val, 0 ) ) )
		{
		    char buffer[20];
		    sprintf( buffer, "%ld", PyInt_AsLong( PyTuple_GetItem( val, 0 ) ) );
		    field->Set( buffer );
		}
		else if ( PyTuple_Size( val ) == 2 &&
			  PyInt_Check( PyTuple_GetItem( val, 0 ) ) &&
			  PyInt_Check( PyTuple_GetItem( val, 1 ) ) )
		{
		    char buffer[40];
		    sprintf( buffer, "%ld/%ld",
			     PyInt_AsLong( PyTuple_GetItem( val, 0 ) ),
			     PyInt_AsLong( PyTuple_GetItem( val, 1 ) ) );
		    field->Set( buffer );
		}
		else
		{
		    delete newframe;
		    newframe = NULL;
		}
	    }
	    else
	    {
		delete newframe;
		newframe = NULL;
	    }

	    if ( newframe == NULL )
	    {
		PyErr_Format( ID3Error, "'%s' attribute must be string or int or (int,) or (int,int)", attrname );
		return -1;
	    }
	    break;
	}

	// if we reach this point, newframe should have a good frame in it.
	// remove all old instances of this type and attach the new frame.
        
	j = 0;
	for ( i = 0; i < self->size; ++i )
	{
	    if ( self->frames[i]->GetID() == p->fid )
		delete self->frames[i];
	    else
		self->frames[j++] = self->frames[i];
	}
	self->size = j;

	if ( self->size + 1 > self->alloc )
	{
	    self->alloc += 8;
	    self->frames = (ID3_Frame**)realloc( self->frames, self->alloc * sizeof( ID3_Frame* ) );
	}

	self->frames[self->size++] = newframe;

	return 0;
    }
    
    PyErr_Format( PyExc_AttributeError, "'%s' object has no attribute '%s'",
		  ID3Type.tp_name, attrname );
    return -1;
    
}



/////////////////////
//
//   creating, updating, destroying tags
//
/////////////////////

static PyObject* id3_new( PyObject* self, PyObject* args )
{
    ID3Object* id3obj;
    char* filename;

    if ( !PyArg_ParseTuple( args, "s:tag", &filename ) )
        return NULL;

    id3obj = PyObject_NEW( ID3Object, &ID3Type );
    id3obj->tag = new ID3_Tag( filename );
    if ( id3obj->tag == NULL )
    {
        PyErr_SetString( ID3Error, "tag constructor failed" );
        
        PyObject_Del( id3obj );
        return NULL;
    }

    // separate all the frames from the object and keep them in an array

    id3obj->alloc = id3obj->tag->NumFrames();
    id3obj->frames = (ID3_Frame**)malloc( id3obj->alloc * sizeof( ID3_Frame* ) );
    id3obj->size = 0;
    ID3_Tag::Iterator* titer = id3obj->tag->CreateIterator();
    ID3_Frame* frame;
    
    while ( (frame = titer->GetNext()) )
    {
	// unfortunately, we have to discard any frames that
	// id3lib doesn't recognize, due to a bug in its handling
	// of them.  hopefully this will change.
	if ( frame->GetID() != ID3FID_NOFRAME )
	{
	    id3obj->frames[id3obj->size] = new ID3_Frame( *frame );
	    ++id3obj->size;
	}
	id3obj->tag->RemoveFrame( frame );
    }

    return (PyObject*) id3obj;
}

static PyObject* id3_update( ID3Object* self )
{
    int i;
    for ( i = 0; i < self->size; ++i )
	self->tag->AddFrame( self->frames[i] );
    
    self->tag->Update();

    ID3_Tag::Iterator* titer = self->tag->CreateIterator();
    ID3_Frame* frame;
    
    while ( (frame = titer->GetNext()) )
    {
	self->tag->RemoveFrame( frame );
    }

    Py_INCREF( Py_None );
    return Py_None;
}
    
static void id3_dealloc( ID3Object* self )
{
    int i;
    
    for ( i = 0; i < self->size; ++i )
	delete self->frames[i];
    free( self->frames );

    delete self->tag;

    PyObject_Del( (PyObject*)self );
}


//////////////////////////
//
//  frame ID querying
//
//////////////////////////

static PyObject* query_frametype( PyObject* self, PyObject* args )
{
    PyObject* result;
    PyObject* obj;
    char* type;
    int i;

    if ( !PyArg_ParseTuple( args, "O", &obj ) )
	return NULL;

    if ( !PyString_Check( obj ) )
    {
	PyErr_SetString( ID3Error, "non-string as frame ID" );
	return NULL;
    }
    
    type = PyString_AsString( obj );
    
    if ( strlen(type) != 4 )
    {
	PyErr_Format( ID3Error, "'%s' is not a legal frame ID", type );
	Py_DECREF( obj );
	return NULL;
    }

    for ( i = 0; i < 4; ++i )
	if ( !(type[i] >= 'A' && type[i] <= 'Z') &&
	     !(type[i] >= '0' && type[i] <= '9') )
	{
	    PyErr_Format( ID3Error, "'%s' is not a legal frame ID", type );
	    Py_DECREF( obj );
	    return NULL;
	}

    result = PyDict_GetItem( frameid_lookup, obj );
    Py_DECREF( obj );
    if ( result == NULL )
    {
	PyErr_Format( ID3Error, "frame ID '%s' is not supported by id3lib", type );
	return NULL;
    }

    Py_INCREF( result );
    return result;
}

    

static PyMethodDef module_methods[] = {
    { "tag", id3_new, METH_VARARGS },
    { "query", query_frametype, METH_VARARGS },
    { NULL, NULL }
};

extern "C" {
    void initpyid3lib( void )
    {
        PyObject *m;
        PyObject *d;
        
        ID3Type.ob_type = &PyType_Type;
        
        m = Py_InitModule( MODULE_NAME, module_methods );
        d = PyModule_GetDict( m );
        ID3Error = PyErr_NewException( MODULE_NAME ".ID3Error", NULL, NULL );
        PyDict_SetItemString( d, "ID3Error", ID3Error );
        
        Py_INCREF( &ID3Type );
	PyModule_AddObject( m, "ID3", (PyObject*)&ID3Type );

	PyModule_AddObject( m, "copyright", PyString_FromString( COPYRIGHT_NOTICE ) );
	PyModule_AddObject( m, "version", PyString_FromString( MODULE_VERSION ) );
	
	for ( magic_attribute_table_size = 0;
	      magic_attribute_table[magic_attribute_table_size].name;
	      ++magic_attribute_table_size )
	    ;

	int i;
	for ( i = ID3FN_NOFIELD; i <= ID3FN_LASTFIELDID; ++i )
	    field_keys[i] = NULL;
	
	field_keys[ID3FN_TEXTENC] = PyString_FromString( "textenc" );
	field_keys[ID3FN_TEXT] = PyString_FromString( "text" );
	field_keys[ID3FN_URL] = PyString_FromString( "url" );
	field_keys[ID3FN_DATA] = PyString_FromString( "data" );
	field_keys[ID3FN_DESCRIPTION] = PyString_FromString( "description" );
	field_keys[ID3FN_OWNER] = PyString_FromString( "owner" );
	field_keys[ID3FN_EMAIL] = PyString_FromString( "email" );
	field_keys[ID3FN_RATING] = PyString_FromString( "rating" );
	field_keys[ID3FN_FILENAME] = PyString_FromString( "filename" );
	field_keys[ID3FN_LANGUAGE] = PyString_FromString( "language" );
	field_keys[ID3FN_PICTURETYPE] = PyString_FromString( "picturetype" );
	field_keys[ID3FN_IMAGEFORMAT] = PyString_FromString( "imageformat" );
	field_keys[ID3FN_MIMETYPE] = PyString_FromString( "mimetype" );
	field_keys[ID3FN_COUNTER] = PyString_FromString( "counter" );
	field_keys[ID3FN_ID] = PyString_FromString( "id" );
	field_keys[ID3FN_VOLUMEADJ] = PyString_FromString( "volumeadj" );
	field_keys[ID3FN_NUMBITS] = PyString_FromString( "numbits" );
	field_keys[ID3FN_VOLCHGRIGHT] = PyString_FromString( "volchgright" );
	field_keys[ID3FN_VOLCHGLEFT] = PyString_FromString( "volchgleft" );
	field_keys[ID3FN_PEAKVOLRIGHT] = PyString_FromString( "peakvolright" );
	field_keys[ID3FN_PEAKVOLLEFT] = PyString_FromString( "peakvolleft" );
	field_keys[ID3FN_TIMESTAMPFORMAT] = PyString_FromString( "timestampformat" );
	field_keys[ID3FN_CONTENTTYPE] = PyString_FromString( "contenttype" );

	frame_id_key_obj = PyString_FromString( "frameid" );

	ID3_FrameInfo finfo;
	frameid_lookup = PyDict_New();

	for ( i = ID3FID_NOFRAME+1; i < ID3FID_LASTFRAMEID; ++i )
	{
	    char *s;
	    s = finfo.LongName( (ID3_FrameID)i );
	    if ( s && strlen(s) == 4 )
	    {
		PyObject* tuple = PyTuple_New( 3 );
		PyTuple_SET_ITEM( tuple, 0, PyInt_FromLong( i ) );
		PyTuple_SET_ITEM( tuple, 1, PyString_FromString( finfo.Description( (ID3_FrameID)i ) ) );
		
		ID3_Frame* frame = new ID3_Frame( (ID3_FrameID)i );
		ID3_Frame::Iterator* fiter = frame->CreateIterator();
		ID3_Field* field;

		// overestimate the size.  "lyst" is a misnomer, it's
		// actually a tuple.
		PyObject* lyst;
		lyst = PyTuple_New( frame->NumFields() );
		int actual = 0;
		
		while( (field = fiter->GetNext()) )
		{
		    ID3_FieldID flid = field->GetID();
		    if ( field_keys[flid] == NULL )
			continue;

		    Py_INCREF( field_keys[flid] );
		    PyTuple_SET_ITEM( lyst, actual, field_keys[flid] );
		    ++actual;
		}
		_PyTuple_Resize( &lyst, actual );

		delete fiter;
		delete frame;

		PyTuple_SET_ITEM( tuple, 2, lyst );

		PyDict_SetItemString( frameid_lookup, s, tuple );
		Py_DECREF( tuple );
	    }
	}
    }
}



