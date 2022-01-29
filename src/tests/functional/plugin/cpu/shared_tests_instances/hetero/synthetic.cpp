// Copyright (C) 2018-2022 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include <vector>

#include "hetero/synthetic.hpp"
#include "ngraph_functions/builders.hpp"
#include "ngraph_functions/subgraph_builders.hpp"

namespace {
using namespace HeteroTests;

// this tests load plugin by library name: this is not available during static linkage
#ifndef OPENVINO_STATIC_LIBRARY

INSTANTIATE_TEST_SUITE_P(smoke_SingleMajorNode, HeteroSyntheticTest,
                        ::testing::Combine(
                                ::testing::Values(std::vector<PluginParameter>{{"CPU0", "ov_intel_cpu_plugin"}, {"CPU1", "ov_intel_cpu_plugin"}}),
                                ::testing::ValuesIn(HeteroTests::HeteroSyntheticTest::_singleMajorNodeFunctions)),
                        HeteroSyntheticTest::getTestCaseName);

INSTANTIATE_TEST_SUITE_P(nightly_RandomMajorNodes, HeteroSyntheticTest,
                        ::testing::Combine(
                                ::testing::Values(std::vector<PluginParameter>{{"CPU0", "ov_intel_cpu_plugin"}, {"CPU1", "ov_intel_cpu_plugin"}}),
                                ::testing::ValuesIn(HeteroTests::HeteroSyntheticTest::_randomMajorNodeFunctions)),
                        HeteroSyntheticTest::getTestCaseName);

#endif // !OPENVINO_STATIC_LIBRARY

}  // namespace
