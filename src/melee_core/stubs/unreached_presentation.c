#include <stdio.h>
#include <stdlib.h>

static void unexpected(const char* symbol)
{
    fprintf(stderr, "unexpected headless presentation call: %s\n", symbol);
    abort();
}

#define UNREACHED(name) void name(void) { unexpected(#name); }

UNREACHED(GXBegin)
UNREACHED(GXCallDisplayList)
UNREACHED(GXClearVtxDesc)
UNREACHED(GXInitTexObj)
UNREACHED(GXInitTexObjCI)
UNREACHED(GXInitTexObjLOD)
UNREACHED(GXInitTlutObj)
UNREACHED(GXLoadNrmMtxImm)
UNREACHED(GXLoadPosMtxImm)
UNREACHED(GXLoadTexMtxImm)
UNREACHED(GXLoadTexObj)
UNREACHED(GXLoadTlut)
UNREACHED(GXPixModeSync)
UNREACHED(GXSetAlphaCompare)
UNREACHED(GXSetAlphaUpdate)
UNREACHED(GXSetArray)
UNREACHED(GXSetBlendMode)
UNREACHED(GXSetChanAmbColor)
UNREACHED(GXSetChanCtrl)
UNREACHED(GXSetChanMatColor)
UNREACHED(GXSetColorUpdate)
UNREACHED(GXSetCullMode)
UNREACHED(GXSetCurrentMtx)
UNREACHED(GXSetDither)
UNREACHED(GXSetDstAlpha)
UNREACHED(GXSetNumChans)
UNREACHED(GXSetNumTevStages)
UNREACHED(GXSetNumTexGens)
UNREACHED(GXSetTevAlphaIn)
UNREACHED(GXSetTevAlphaOp)
UNREACHED(GXSetTevColor)
UNREACHED(GXSetTevColorIn)
UNREACHED(GXSetTevColorOp)
UNREACHED(GXSetTevColorS10)
UNREACHED(GXSetTevKAlphaSel)
UNREACHED(GXSetTevKColor)
UNREACHED(GXSetTevKColorSel)
UNREACHED(GXSetTevOp)
UNREACHED(GXSetTevOrder)
UNREACHED(GXSetTevSwapMode)
UNREACHED(GXSetTexCoordGen2)
UNREACHED(GXSetVtxAttrFmt)
UNREACHED(GXSetVtxDesc)
UNREACHED(GXSetZCompLoc)
UNREACHED(GXSetZMode)

UNREACHED(HSD_CObjGetCurrent)
UNREACHED(HSD_CObjGetInvViewingMtxPtrDirect)
UNREACHED(HSD_JObjDisp)
UNREACHED(HSD_JObjDispSub)
UNREACHED(HSD_JObjMakePositionMtx)
UNREACHED(HSD_LObjGetActiveByID)
UNREACHED(HSD_LObjGetActiveByIndex)
UNREACHED(HSD_LObjGetCurrentByType)
UNREACHED(HSD_LObjGetLightMaskAlpha)
UNREACHED(HSD_LObjGetLightMaskDiffuse)
UNREACHED(HSD_LObjGetLightMaskSpecular)
UNREACHED(HSD_LObjGetLightVector)
UNREACHED(HSD_LObjGetNbActive)
UNREACHED(HSD_LObjSetup)
UNREACHED(_HSD_mkEnvelopeModelNodeMtx)
UNREACHED(HSD_ByteCodeEval)

// Rendering-only profiling is intentionally suppressed in the headless port.
unsigned char HSD_PerfCurrentStat[256];
void HSD_PerfCountEnvelopeBlending(int count) { (void) count; }
