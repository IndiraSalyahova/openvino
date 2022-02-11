// Copyright (C) 2018-2022 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include <map>
#include <string>

#include "openvino/core/core_visibility.hpp"
#include "openvino/core/node_vector.hpp"
#include "openvino/core/rtti.hpp"
#include "openvino/core/type.hpp"

namespace ov {
class Node;
class AttributeVisitor;
class Any;

class OPENVINO_API RuntimeAttribute {
public:
    static const DiscreteTypeInfo& get_type_info_static() {
        static const ::ov::DiscreteTypeInfo type_info{"RuntimeAttribute", 0};
        return type_info;
    }
    virtual const DiscreteTypeInfo& get_type_info() const {
        return get_type_info_static();
    }
    using Ptr = std::shared_ptr<RuntimeAttribute>;
    using Base = std::tuple<::ov::RuntimeAttribute>;
    virtual ~RuntimeAttribute() = default;
    virtual bool is_copyable() const;
    virtual Any init(const std::shared_ptr<Node>& node) const;
    virtual Any merge(const ov::NodeVector& nodes) const;
    virtual Any merge(const ov::OutputVector& outputs) const;
    virtual std::string to_string() const;
    virtual bool visit_attributes(AttributeVisitor&);
    bool visit_attributes(AttributeVisitor& visitor) const {
        return const_cast<RuntimeAttribute*>(this)->visit_attributes(visitor);
    }
};
OPENVINO_API std::ostream& operator<<(std::ostream& os, const RuntimeAttribute& attrubute);

}  // namespace ov
