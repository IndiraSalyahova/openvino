// Copyright (C) 2018-2022 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include "openvino/op/util/binary_elementwise_comparison.hpp"

namespace ov {
namespace op {
namespace v1 {
/// \brief Elementwise less-than-or-equal operation.
class OPENVINO_API LessEqual : public util::BinaryElementwiseComparison {
public:
    OPENVINO_OP("LessEqual", "opset1", op::util::BinaryElementwiseComparison, 1);
    BWDCMP_RTTI_DECLARATION;
    /// \brief Constructs a less-than-or-equal operation.
    LessEqual() : util::BinaryElementwiseComparison(AutoBroadcastType::NUMPY) {}

    /// \brief Constructs a less-than-or-equal operation.
    ///
    /// \param arg0 Node that produces the first input tensor.
    /// \param arg1 Node that produces the second input tensor.
    /// \param auto_broadcast Auto broadcast specification
    LessEqual(const Output<Node>& arg0,
              const Output<Node>& arg1,
              const AutoBroadcastSpec& auto_broadcast = AutoBroadcastSpec(AutoBroadcastType::NUMPY));

    std::shared_ptr<Node> clone_with_new_inputs(const OutputVector& new_args) const override;
    OPENVINO_SUPPRESS_DEPRECATED_START
    bool evaluate(const HostTensorVector& outputs, const HostTensorVector& inputs) const override;
    OPENVINO_SUPPRESS_DEPRECATED_END
    bool has_evaluate() const override;
};
}  // namespace v1
}  // namespace op
}  // namespace ov
