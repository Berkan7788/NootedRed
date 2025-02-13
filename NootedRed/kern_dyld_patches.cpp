//  Copyright © 2022-2023 ChefKiss Inc. Licensed under the Thou Shalt Not Profit License version 1.5. See LICENSE for
//  details.

#include "kern_dyld_patches.hpp"
#include "kern_nred.hpp"
#include <Headers/kern_api.hpp>
#include <Headers/kern_devinfo.hpp>
#include <IOKit/IODeviceTreeSupport.h>

DYLDPatches *DYLDPatches::callback = nullptr;

void DYLDPatches::init() { callback = this; }

void DYLDPatches::processPatcher(KernelPatcher &patcher) {
    if (!(lilu.getRunMode() & LiluAPI::RunningNormal) || !checkKernelArgument("-nredvcn")) { return; }

    auto *entry = IORegistryEntry::fromPath("/", gIODTPlane);
    if (entry) {
        DBGLOG("dyld", "Setting hwgva-id to iMacPro1,1");
        entry->setProperty("hwgva-id", const_cast<char *>(kHwGvaId), arrsize(kHwGvaId));
        entry->release();
    }

    KernelPatcher::RouteRequest request {"_cs_validate_page", csValidatePage, this->orgCsValidatePage};

    PANIC_COND(!patcher.routeMultipleLong(KernelPatcher::KernelID, &request, 1), "dyld",
        "Failed to route kernel symbols");
}

void DYLDPatches::csValidatePage(vnode *vp, memory_object_t pager, memory_object_offset_t page_offset, const void *data,
    int *validated_p, int *tainted_p, int *nx_p) {
    FunctionCast(csValidatePage, callback->orgCsValidatePage)(vp, pager, page_offset, data, validated_p, tainted_p,
        nx_p);

    char path[PATH_MAX];
    int pathlen = PATH_MAX;
    if (vn_getpath(vp, path, &pathlen)) { return; }

    if (!UserPatcher::matchSharedCachePath(path)) {
        if (LIKELY(strncmp(path, kCoreLSKDMSEPath, arrsize(kCoreLSKDMSEPath))) ||
            LIKELY(strncmp(path, kCoreLSKDPath, arrsize(kCoreLSKDPath)))) {
            return;
        }
        const DYLDPatch patch = {kCoreLSKDOriginal, kCoreLSKDPatched, "CoreLSKD streaming CPUID to Haswell"};
        patch.apply(const_cast<void *>(data), PAGE_SIZE);
        return;
    }

    if (UNLIKELY(KernelPatcher::findAndReplace(const_cast<void *>(data), PAGE_SIZE, kVideoToolboxDRMModelOriginal,
            arrsize(kVideoToolboxDRMModelOriginal), BaseDeviceInfo::get().modelIdentifier, 20))) {
        DBGLOG("dyld", "Applied 'VideoToolbox DRM model check' patch");
    }

    const DYLDPatch patches[] = {
        {kAGVABoardIdOriginal, kAGVABoardIdPatched, "iMacPro1,1 spoof (AppleGVA)"},
        {kHEVCEncBoardIdOriginal, kHEVCEncBoardIdPatched, "iMacPro1,1 spoof (AppleGVAHEVCEncoder)"},
    };
    DYLDPatch::applyAll(patches, const_cast<void *>(data), PAGE_SIZE);

    if (getKernelVersion() >= KernelVersion::Ventura) {
        const DYLDPatch patches[] = {
            {kVAAcceleratorInfoIdentifyVenturaOriginal, kVAAcceleratorInfoIdentifyVenturaOriginalMask,
                kVAAcceleratorInfoIdentifyVenturaPatched, kVAAcceleratorInfoIdentifyVenturaPatchedMask,
                "VAAcceleratorInfo::identify"},
            {kVAFactoryCreateGraphicsEngineAndBltVenturaOriginal,
                kVAFactoryCreateGraphicsEngineAndBltVenturaOriginalMask,
                kVAFactoryCreateGraphicsEngineAndBltVenturaPatched,
                kVAFactoryCreateGraphicsEngineAndBltVenturaPatchedMask,
                "VAFactory::createGraphicsEngine/VAFactory::createImageBlt"},
            {kVAFactoryCreateVPVenturaOriginal, kVAFactoryCreateVPVenturaOriginalMask, kVAFactoryCreateVPVenturaPatched,
                kVAFactoryCreateVPVenturaPatchedMask, "VAFactory::create*VP"},
        };
        DYLDPatch::applyAll(patches, const_cast<void *>(data), PAGE_SIZE);
    } else {
        const DYLDPatch patches[] = {
            {kVAAcceleratorInfoIdentifyOriginal, kVAAcceleratorInfoIdentifyOriginalMask,
                kVAAcceleratorInfoIdentifyPatched, kVAAcceleratorInfoIdentifyPatchedMask,
                "VAAcceleratorInfo::identify"},
            {kVAFactoryCreateGraphicsEngineOriginal, kVAFactoryCreateGraphicsEngineOriginalMask,
                kVAFactoryCreateGraphicsEnginePatched, kVAFactoryCreateGraphicsEnginePatchedMask,
                "VAFactory::createGraphicsEngine"},
            {kVAFactoryCreateImageBltOriginal, kVAFactoryCreateImageBltMask, kVAFactoryCreateImageBltPatched,
                kVAFactoryCreateImageBltPatchedMask, "VAFactory::createImageBlt"},
            {kVAFactoryCreateVPOriginal, kVAFactoryCreateVPOriginalMask, kVAFactoryCreateVPPatched,
                kVAFactoryCreateVPPatchedMask, "VAFactory::create*VP"},
        };
        DYLDPatch::applyAll(patches, const_cast<void *>(data), PAGE_SIZE);
    }

    const DYLDPatch patch = {kVAAddrLibInterfaceInitOriginal, kVAAddrLibInterfaceInitOriginalMask,
        kVAAddrLibInterfaceInitPatched, kVAAddrLibInterfaceInitPatchedMask, "VAAddrLibInterface::init"};
    patch.apply(const_cast<void *>(data), PAGE_SIZE);

    // ----------------------------------------------
    if (NRed::callback->chipType >= ChipType::Renoir) { return; }    // Everything after is for VCN 1
    // ----------------------------------------------

    const DYLDPatch vcn1Patches[] = {
        {kWriteUvdNoOpOriginal, kWriteUvdNoOpPatched, "Vcn2DecCommand::writeUvdNoOp"},
        {kWriteUvdEngineStartOriginal, kWriteUvdEngineStartPatched, "Vcn2DecCommand::writeUvdEngineStart"},
        {kWriteUvdGpcomVcpuCmdOriginal, kWriteUvdGpcomVcpuCmdPatched, "Vcn2DecCommand::writeUvdGpcomVcpuCmdOriginal"},
        {kWriteUvdGpcomVcpuData0Original, kWriteUvdGpcomVcpuData0Patched,
            "Vcn2DecCommand::writeUvdGpcomVcpuData0Original"},
        {kWriteUvdGpcomVcpuData1Original, kWriteUvdGpcomVcpuData1Patched,
            "Vcn2DecCommand::writeUvdGpcomVcpuData1Original"},
        {kAddEncodePacketOriginal, kAddEncodePacketPatched, "Vcn2EncCommand::addEncodePacket"},
        {kAddSliceHeaderPacketOriginal, kAddSliceHeaderPacketMask, kAddSliceHeaderPacketPatched,
            kAddSliceHeaderPacketMask, "Vcn2EncCommand::addSliceHeaderPacket"},
        {kAddIntraRefreshPacketOriginal, kAddIntraRefreshPacketMask, kAddIntraRefreshPacketPatched,
            kAddIntraRefreshPacketMask, "Vcn2EncCommand::addIntraRefreshPacket"},
        {kAddContextBufferPacketOriginal, kAddContextBufferPacketPatched, "Vcn2EncCommand::addContextBufferPacket"},
        {kAddBitstreamBufferPacketOriginal, kAddBitstreamBufferPacketPatched,
            "Vcn2EncCommand::addBitstreamBufferPacket"},
        {kAddFeedbackBufferPacketOriginal, kAddFeedbackBufferPacketPatched, "Vcn2EncCommand::addFeedbackBufferPacket"},
        {kAddInputFormatPacketOriginal, kAddFormatPacketOriginalMask, kAddFormatPacketPatched,
            kAddFormatPacketPatchedMask, "Vcn2EncCommand::addInputFormatPacket"},
        {kAddOutputFormatPacketOriginal, kAddFormatPacketOriginalMask, kAddFormatPacketPatched,
            kAddFormatPacketPatchedMask, "Vcn2EncCommand::addOutputFormatPacket"},
    };
    DYLDPatch::applyAll(vcn1Patches, const_cast<void *>(data), PAGE_SIZE);
}
