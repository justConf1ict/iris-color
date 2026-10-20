#include "IrisPlugin.h"
#include "public.sdk/source/main/pluginfactory.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

BEGIN_FACTORY_DEF("Independent Audio Lab", "", "")
    DEF_CLASS2(INLINE_UID_FROM_FUID(iris::vst3::kProcessorUID), PClassInfo::kManyInstances,
        kVstAudioEffectClass, "Iris Color", Vst::kDistributable,
        "Fx|Pitch Shift|Filter", "0.7.0", kVstVersionString, iris::vst3::Processor::createInstance)
    DEF_CLASS2(INLINE_UID_FROM_FUID(iris::vst3::kControllerUID), PClassInfo::kManyInstances,
        kVstComponentControllerClass, "Iris Color Controller", 0, "", "0.7.0",
        kVstVersionString, iris::vst3::Controller::createInstance)
END_FACTORY
