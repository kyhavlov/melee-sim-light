#include "runtime/scalar.h"

#include <baselib/aobj.h>
#include <baselib/class.h>
#include <baselib/cobj.h>
#include <baselib/dobj.h>
#include <baselib/fobj.h>
#include <baselib/gobj.h>
#include <baselib/gobjproc.h>
#include <baselib/id.h>
#include <baselib/jobj.h>
#include <baselib/list.h>
#include <baselib/lobj.h>
#include <baselib/mobj.h>
#include <baselib/pobj.h>
#include <baselib/robj.h>
#include <baselib/tobj.h>
#include <baselib/wobj.h>
#include <melee/cm/types.h>
#include <melee/ft/dobjlist.h>
#include <melee/ft/types.h>
#include <melee/it/itCharItems.h>
#include <melee/it/types.h>
#include <melee/mp/types.h>

#define MSL_RELOC_TYPE(id, type) type* msl_reloc_force_##id;
#include "runtime/relocation_types.def"
#undef MSL_RELOC_TYPE
