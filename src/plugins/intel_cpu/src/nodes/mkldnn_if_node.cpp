// Copyright (C) 2018-2022 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include "mkldnn_if_node.h"

#include "mkldnn_extension_utils.h"
#include "ie_ngraph_utils.hpp"
#include "transformations/utils/utils.hpp"
#include "common/cpu_memcpy.h"

#include <string>
#include <vector>

using namespace MKLDNNPlugin;

MKLDNNIfNode::PortMapHelper::PortMapHelper(const MKLDNNMemoryPtr &from, const std::deque<MKLDNNMemoryPtr>& to,
                                           const mkldnn::engine& eng) : srcMemPtr(from), dstMemPtrs(to) {
    if (srcMemPtr->getDesc().isDefined())
        size = srcMemPtr->GetSize();
}

void MKLDNNIfNode::PortMapHelper::execute(mkldnn::stream& strm) {
    // if output shapes are changed,
    // after subgraph inference we should redefine out memory of 'If'
    redefineTo();

    cpu_memcpy(dstMemPtrs.front()->GetPtr(), srcMemPtr->GetPtr(), size);
}

void MKLDNNIfNode::PortMapHelper::redefineTo() {
    const auto &currDesc = dstMemPtrs.front()->getDesc();
    if (currDesc.getShape().isDynamic() || currDesc.getShape().getStaticDims() != srcMemPtr->getStaticDims()) {
        // WA [DS] : need to rewrite it. Copypaste from MKLDNNNode::redefineOutputMemory
        // this path is necessary if there are several edges per one port
        // in this case edge memory share same physical memory
        // so we need to find which edge allocate memory, reallocate memory and share this memory between other edges
        size_t sharedEdgeNum = 0;
        for (size_t j = 0; j < dstMemPtrs.size(); j++) {
            if (!dstMemPtrs[j]->isUsedExternalStorage()) {
                sharedEdgeNum = j;
                break;
            }
        }

        const auto &memDesc = srcMemPtr->getDesc();
        dstMemPtrs[sharedEdgeNum]->redefineDesc(memDesc);
        void *data = dstMemPtrs[sharedEdgeNum]->GetData();
        for (size_t j = 0; j < dstMemPtrs.size(); j++) {
            if (j == sharedEdgeNum)
                continue;
            dstMemPtrs[j]->redefineDesc(memDesc, data);
        }

        size = srcMemPtr->GetSize();
    }
}

bool MKLDNNIfNode::isSupportedOperation(const std::shared_ptr<const ov::Node>& op, std::string& errorMessage) noexcept {
    try {
        if (!one_of(op->get_type_info(), ov::op::v8::If::get_type_info_static())) {
            errorMessage = "Not supported If operation version " + std::to_string(op->get_type_info().version) +
                    " with name '" + op->get_friendly_name() + "'. Node If supports only opset8 version.";
            return false;
        }
    } catch (...) {
        return false;
    }
    return true;
}

MKLDNNIfNode::MKLDNNIfNode(const std::shared_ptr<ov::Node>& op, const mkldnn::engine& eng, MKLDNNWeightsSharing::Ptr &cache) :
        MKLDNNNode(op, eng, cache), ovOp(op) {
    std::string errorMessage;
    if (!isSupportedOperation(op, errorMessage)) {
        IE_THROW(NotImplemented) << errorMessage;
    }
}

void MKLDNNIfNode::getSupportedDescriptors() {
    auto ifOp = ov::as_type_ptr<ov::op::v8::If>(ovOp);

    const std::shared_ptr<const ov::Model>& thenBody = ifOp->get_then_body();
    const std::shared_ptr<const ov::Model>& elseBody = ifOp->get_else_body();
    subGraphThen.CreateGraph(thenBody, ext_mng, weightCache);
    subGraphElse.CreateGraph(elseBody, ext_mng, weightCache);

    const auto &inMapThen = subGraphThen.GetInputNodesMap();
    for (const auto &param : ifOp->get_then_body()->get_parameters()) {
        auto inNode = inMapThen.find(param->get_friendly_name());
        if (inNode != inMapThen.end()) {
            inputMemThen.push_back(getToMemories(inNode->second.get(), 0));
        } else {
            IE_THROW() << "Then body of node If with name " << getName() << " does not have input with name: "
                    << param->get_friendly_name();
        }
    }

    const auto &inMapElse = subGraphElse.GetInputNodesMap();
    for (const auto &param : ifOp->get_else_body()->get_parameters()) {
        auto inNode = inMapElse.find(param->get_friendly_name());
        if (inNode != inMapElse.end()) {
            inputMemElse.push_back(getToMemories(inNode->second.get(), 0));
        } else {
            IE_THROW() << "Else body of node If with name " << getName() << " does not have input with name: "
                    << param->get_friendly_name();
        }
    }

    const auto &outMapThen = subGraphThen.GetOutputNodesMap();
    for (const auto& out : ifOp->get_then_body()->get_results()) {
        const auto prev = out->input_value(0);
        const std::string inputID = ngraph::op::util::get_ie_output_name(prev);
        auto outNode = outMapThen.find(inputID);
        if (outNode != outMapThen.end()) {
            auto outMem = outNode->second->getParentEdgeAt(0)->getMemoryPtr();
            outputMemThen.push_back(outMem);
        } else {
            IE_THROW() << "Then body of node If with name " << getName() << " does not have output with name: "
                    << inputID;
        }
    }

    const auto &outMapElse = subGraphElse.GetOutputNodesMap();
    for (const auto& out : ifOp->get_else_body()->get_results()) {
        const auto prev = out->input_value(0);
        const std::string inputID = ngraph::op::util::get_ie_output_name(prev);
        auto outNode = outMapElse.find(inputID);
        if (outNode != outMapElse.end()) {
            auto outMem = outNode->second->getParentEdgeAt(0)->getMemoryPtr();
            outputMemElse.push_back(outMem);
        } else {
            IE_THROW() << "Else body of node If with name " << getName() << " does not have output with name: "
                    << inputID;
        }
    }

    // Port map: outputs
    for (const auto& desc : ifOp->get_output_descriptions(0)) {
        auto body_output_idx = desc->m_body_value_index;
        thenOutputPortMap.emplace_back(PortMap {
            static_cast<int>(desc->m_output_index), static_cast<int>(body_output_idx)});
    }
    for (const auto& desc : ifOp->get_output_descriptions(1)) {
        auto body_output_idx = desc->m_body_value_index;
        elseOutputPortMap.emplace_back(PortMap {
            static_cast<int>(desc->m_output_index), static_cast<int>(body_output_idx)});
    }

    for (const auto& desc : ifOp->get_input_descriptions(0)) {
        auto body_input_index = desc->m_body_parameter_index;
        thenInputPortMap.emplace_back(PortMap {
            static_cast<int>(desc->m_input_index), static_cast<int>(body_input_index)});
    }
    for (const auto& desc : ifOp->get_input_descriptions(1)) {
        auto body_input_index = desc->m_body_parameter_index;
        elseInputPortMap.emplace_back(PortMap {
            static_cast<int>(desc->m_input_index), static_cast<int>(body_input_index)});
    }
}

void MKLDNNIfNode::initSupportedPrimitiveDescriptors() {
    if (!supportedPrimitiveDescriptors.empty())
        return;

    NodeConfig config;
    config.inConfs.reserve(getParentEdges().size());
    config.outConfs.reserve(getChildEdges().size());

    for (size_t i = 0; i < inputShapes.size(); i++) {
        PortConfig dataConf {};
        auto descCreator = BlockedDescCreator::getCommonCreators().at(LayoutType::ncsp);
        dataConf.desc = descCreator->createSharedDesc(getOriginalInputPrecisionAtPort(i), getInputShapeAtPort(i));
        config.inConfs.emplace_back(dataConf);
    }

    for (size_t i = 0; i < outputShapes.size(); i++) {
        PortConfig dataConf {};
        auto descCreator = BlockedDescCreator::getCommonCreators().at(LayoutType::ncsp);
        dataConf.desc = descCreator->createSharedDesc(getOriginalOutputPrecisionAtPort(i), getOutputShapeAtPort(i));
        config.outConfs.push_back(dataConf);
    }

    config.dynBatchSupport = true;

    supportedPrimitiveDescriptors.emplace_back(config, impl_desc_type::unknown);
}

void MKLDNNIfNode::createPrimitive() {
    const auto& eng = getEngine();
    prepareBeforeMappers(true, eng);
    prepareBeforeMappers(false, eng);
    prepareAfterMappers(true, eng);
    prepareAfterMappers(false, eng);

    if (inputShapesDefined()) {
        updateLastInputDims();
    }
}

void MKLDNNIfNode::prepareBeforeMappers(const bool isThen, const dnnl::engine& eng) {
    auto &inputPortMap = isThen ? thenInputPortMap : elseInputPortMap;
    auto &inputMems = isThen ? inputMemThen : inputMemElse;
    auto &beforeMappers = isThen ? beforeThenMappers : beforeElseMappers;
    for (auto& map_rule : inputPortMap) {
        auto &fromMem = getParentEdgesAtPort(map_rule.from)[0]->getMemoryPtr();
        auto &toMems = inputMems[map_rule.to];

        beforeMappers.emplace_back(std::make_shared<PortMapHelper>(fromMem, toMems, eng));
    }
}

void MKLDNNIfNode::prepareAfterMappers(const bool isThen, const dnnl::engine& eng) {
    auto &outputPortMap = isThen ? thenOutputPortMap : elseOutputPortMap;
    auto &outputMems = isThen ? outputMemThen : outputMemElse;
    auto &afterMappers = isThen ? afterThenMappers : afterElseMappers;
    for (auto& map_rule : outputPortMap) {
        auto toMems = getToMemories(this, map_rule.from);
        auto &fromMem = outputMems[map_rule.to];

        afterMappers.emplace_back(std::make_shared<PortMapHelper>(fromMem, toMems, eng));
    }
}

std::deque<MKLDNNMemoryPtr> MKLDNNIfNode::getToMemories(const MKLDNNNode* node, const size_t port) const {
    std::deque<MKLDNNMemoryPtr> memories;
    for (auto edge : node->getChildEdgesAtPort(port))
        memories.push_back(edge->getMemoryPtr());
    return memories;
}

void MKLDNNIfNode::execute(mkldnn::stream strm) {
    const bool condition = static_cast<const bool>((reinterpret_cast<const uint8_t*>(getParentEdgeAt(0)->getMemoryPtr()->GetPtr()))[0]);

    auto& beforeMappers = condition ? beforeThenMappers : beforeElseMappers;
    auto& afterMappers = condition ? afterThenMappers : afterElseMappers;
    auto& subGraph = condition ? subGraphThen : subGraphElse;

    for (auto &mapper : beforeMappers)
        mapper->execute(strm);
    subGraph.ResetInferCount();
    subGraph.Infer();
    for (auto &mapper : afterMappers)
        mapper->execute(strm);
}

void MKLDNNIfNode::executeDynamicImpl(mkldnn::stream strm) {
    execute(strm);
}

bool MKLDNNIfNode::created() const {
    return getType() == If;
}

REG_MKLDNN_PRIM_FOR(MKLDNNIfNode, If);
