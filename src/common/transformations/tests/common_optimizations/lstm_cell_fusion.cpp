// Copyright (C) 2023 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include "transformations/common_optimizations/lstm_cell_fusion.hpp"

#include "common_test_utils/ov_test_utils.hpp"
#include "openvino/op/abs.hpp"
#include "openvino/op/add.hpp"
#include "openvino/op/concat.hpp"
#include "openvino/op/constant.hpp"
#include "openvino/op/lstm_cell.hpp"
#include "openvino/op/matmul.hpp"
#include "openvino/op/multiply.hpp"
#include "openvino/op/parameter.hpp"
#include "openvino/op/sigmoid.hpp"
#include "openvino/op/split.hpp"
#include "openvino/op/tanh.hpp"
#include "openvino/pass/constant_folding.hpp"

using namespace ov;

TEST_F(TransformationTestsF, LSTMCellFusion) {
    size_t input_size = 3;
    size_t hidden_size = 2;
    {
        auto X = std::make_shared<op::v0::Parameter>(element::f32, Shape{1, input_size});
        auto H = std::make_shared<op::v0::Parameter>(element::f32, Shape{1, hidden_size});
        auto C = std::make_shared<op::v0::Parameter>(element::f32, Shape{1, hidden_size});
        auto concat = std::make_shared<op::v0::Concat>(OutputVector{X, H}, 1);
        Shape WR_shape{4 * hidden_size, input_size + hidden_size};
        std::vector<float> WR_values(shape_size(WR_shape));
        std::iota(WR_values.begin(), WR_values.end(), 0.0f);
        auto WR = op::v0::Constant::create(element::f32, WR_shape, WR_values);
        auto matmul = std::make_shared<op::v0::MatMul>(concat, WR, false, true);
        Shape B_shape{1, 4 * hidden_size};
        std::vector<float> B_values(shape_size(B_shape));
        std::iota(B_values.begin(), B_values.end(), 0.0f);
        auto B = op::v0::Constant::create(element::f32, B_shape, B_values);
        auto biasadd = std::make_shared<op::v1::Add>(matmul, B);
        auto one = op::v0::Constant::create(element::i32, Shape{}, {1});
        auto split = std::make_shared<op::v1::Split>(biasadd, one /* axis */, 4 /* num splits */);
        auto it = std::make_shared<op::v0::Sigmoid>(split->output(0));
        auto ct = std::make_shared<op::v0::Tanh>(split->output(1));
        auto ft = std::make_shared<op::v0::Sigmoid>(
            std::make_shared<op::v1::Add>(split->output(2), op::v0::Constant::create(element::f32, Shape{1, 1}, {1})));
        auto ot = std::make_shared<op::v0::Sigmoid>(split->output(3));
        auto mul = std::make_shared<op::v1::Multiply>(it, ct);
        auto mul1 = std::make_shared<op::v1::Multiply>(ft, C);
        auto Ct = std::make_shared<op::v1::Add>(mul, mul1);
        auto Ht = std::make_shared<op::v1::Multiply>(std::make_shared<op::v0::Tanh>(Ct), ot);
        auto C_abs = std::make_shared<op::v0::Abs>(Ct);
        auto H_abs = std::make_shared<op::v0::Abs>(Ht);
        model = std::make_shared<Model>(NodeVector{H_abs, C_abs}, ParameterVector{X, H, C});
        manager.register_pass<ov::pass::LSTMCellFusion>();
    }

    {
        auto X = std::make_shared<op::v0::Parameter>(element::f32, Shape{1, input_size});
        auto H = std::make_shared<op::v0::Parameter>(element::f32, Shape{1, hidden_size});
        auto C = std::make_shared<op::v0::Parameter>(element::f32, Shape{1, hidden_size});
        auto concat = std::make_shared<op::v0::Concat>(OutputVector{X, H}, 1);
        Shape W_shape{4 * hidden_size, input_size};
        Shape R_shape{4 * hidden_size, hidden_size};
        std::vector<float> W_values{
            20, 21, 22, 25, 26, 27, 0, 1, 2, 5, 6, 7, 10, 11, 12, 15, 16, 17, 30, 31, 32, 35, 36, 37,
        };
        auto W = op::v0::Constant::create(element::f32, W_shape, W_values);
        std::vector<float> R_values{
            23,
            24,
            28,
            29,
            3,
            4,
            8,
            9,
            13,
            14,
            18,
            19,
            33,
            34,
            38,
            39,
        };
        auto R = op::v0::Constant::create(element::f32, R_shape, R_values);
        Shape B_shape{4 * hidden_size};
        std::vector<float> B_values{5, 6, 0, 1, 2, 3, 6, 7};
        auto B = op::v0::Constant::create(element::f32, B_shape, B_values);
        auto lstm_cell = std::make_shared<op::v4::LSTMCell>(X,
                                                            H,
                                                            C,
                                                            W,
                                                            R,
                                                            B,
                                                            hidden_size,
                                                            std::vector<std::string>{"sigmoid", "tanh", "tanh"});
        auto C_abs = std::make_shared<op::v0::Abs>(lstm_cell->output(1));
        auto H_abs = std::make_shared<op::v0::Abs>(lstm_cell->output(0));
        model_ref = std::make_shared<Model>(NodeVector{H_abs, C_abs}, ParameterVector{X, H, C});
        manager.register_pass<ov::pass::LSTMCellFusion>();
    }

    comparator.enable(FunctionsComparator::CmpValues::CONST_VALUES);
    comparator.enable(FunctionsComparator::CmpValues::ATTRIBUTES);
    comparator.enable(FunctionsComparator::CmpValues::ACCURACY);
}
