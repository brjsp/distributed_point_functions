// Copyright 2021 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "dpf/distributed_point_function.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "dpf/internal/status_matchers.h"

namespace private_statistics {
namespace dpf {
namespace {
using dpf_internal::IsOkAndHolds;
using dpf_internal::StatusIs;
using ::testing::Ne;
using ::testing::StartsWith;

TEST(DistributedPointFunction, TestCreate) {
  for (int log_domain_size = 0; log_domain_size <= 63; ++log_domain_size) {
    for (int element_bitsize = 1; element_bitsize <= 128;
         element_bitsize *= 2) {
      DpfParameters parameters;

      parameters.set_log_domain_size(log_domain_size);
      parameters.set_element_bitsize(element_bitsize);

      EXPECT_THAT(DistributedPointFunction::Create(parameters),
                  IsOkAndHolds(Ne(nullptr)))
          << "log_domain_size=" << log_domain_size
          << " element_bitsize=" << element_bitsize;
    }
  }
}

TEST(DistributedPointFunction, TestCreateCreateIncrementalLargeDomain) {
  std::vector<DpfParameters> parameters(2);
  parameters[0].set_element_bitsize(128);
  parameters[1].set_element_bitsize(128);

  // Test that creating an incremental DPF with a large total domain size works.
  parameters[0].set_log_domain_size(50);
  parameters[1].set_log_domain_size(100);

  EXPECT_THAT(DistributedPointFunction::CreateIncremental(parameters),
              IsOkAndHolds(Ne(nullptr)));
}

TEST(DistributedPointFunction, FailsWithoutParameters) {
  EXPECT_THAT(DistributedPointFunction::CreateIncremental({}),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       "`parameters` must not be empty"));
}

TEST(DistributedPointFunction, FailsWhenParametersNotSorted) {
  std::vector<DpfParameters> parameters(2);

  parameters[0].set_log_domain_size(12);
  parameters[1].set_log_domain_size(10);
  parameters[0].set_element_bitsize(32);
  parameters[1].set_element_bitsize(32);

  EXPECT_THAT(DistributedPointFunction::CreateIncremental(parameters),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       "`log_domain_size` fields must be in ascending order in "
                       "`parameters`"));
}

TEST(DistributedPointFunction, FailsWhenDomainSizeNegative) {
  DpfParameters parameters;

  parameters.set_log_domain_size(-1);
  parameters.set_element_bitsize(32);

  EXPECT_THAT(DistributedPointFunction::Create(parameters),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       "`log_domain_size` must be non-negative"));
}

TEST(DistributedPointFunction, FailsWhenElementBitsizeZeroOrNegative) {
  for (int element_bitsize : {0, -1}) {
    DpfParameters parameters;

    parameters.set_log_domain_size(10);
    parameters.set_element_bitsize(element_bitsize);

    EXPECT_THAT(DistributedPointFunction::Create(parameters),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         "`element_bitsize` must be positive"));
  }
}

TEST(DistributedPointFunction, FailsWhenElementBitsizeTooLarge) {
  DpfParameters parameters;

  parameters.set_log_domain_size(10);
  parameters.set_element_bitsize(256);

  EXPECT_THAT(DistributedPointFunction::Create(parameters),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       "`element_bitsize` must be less than or equal to 128"));
}

TEST(DistributedPointFunction, FailsWhenElementBitsizeNotAPowerOfTwo) {
  DpfParameters parameters;

  parameters.set_log_domain_size(10);
  parameters.set_element_bitsize(23);

  EXPECT_THAT(DistributedPointFunction::Create(parameters),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       "`element_bitsize` must be a power of 2"));
}

TEST(DistributedPointFunction, FailsWhenElementBitsizesDecrease) {
  std::vector<DpfParameters> parameters(2);
  parameters[0].set_log_domain_size(10);
  parameters[1].set_log_domain_size(12);

  parameters[0].set_element_bitsize(128);
  parameters[1].set_element_bitsize(32);

  EXPECT_THAT(DistributedPointFunction::CreateIncremental(parameters),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       "`element_bitsize` fields must be non-decreasing in "
                       "`parameters`"));
}

TEST(DistributedPointFunction, FailsWhenHierarchiesAreTooFarApart) {
  std::vector<DpfParameters> parameters(2);
  parameters[0].set_element_bitsize(128);
  parameters[1].set_element_bitsize(128);

  parameters[0].set_log_domain_size(10);
  parameters[1].set_log_domain_size(74);

  EXPECT_THAT(
      DistributedPointFunction::CreateIncremental(parameters),
      StatusIs(absl::StatusCode::kInvalidArgument,
               "Hierarchies may be at most 63 levels apart after packing"));
}

class DpfKeyGenerationTest
    : public testing::TestWithParam<std::tuple<int, int>> {
 public:
  void SetUp() {
    std::tie(log_domain_size_, element_bitsize_) = GetParam();
    DpfParameters parameters;
    parameters.set_log_domain_size(log_domain_size_);
    parameters.set_element_bitsize(element_bitsize_);
    DPF_ASSERT_OK_AND_ASSIGN(dpf_,
                             DistributedPointFunction::Create(parameters));
  }

 protected:
  int log_domain_size_;
  int element_bitsize_;
  std::unique_ptr<DistributedPointFunction> dpf_;
};

TEST_P(DpfKeyGenerationTest, KeyHasCorrectFormat) {
  DpfKey key_a, key_b;
  DPF_ASSERT_OK_AND_ASSIGN(std::tie(key_a, key_b), dpf_->GenerateKeys(0, 0));

  // Check that party is set correctly.
  EXPECT_EQ(key_a.party(), 0);
  EXPECT_EQ(key_b.party(), 1);
  // Check that keys are accepted by `CreateEvaluationContext`.
  DPF_EXPECT_OK(dpf_->CreateEvaluationContext(key_a));
  DPF_EXPECT_OK(dpf_->CreateEvaluationContext(key_b));
}

TEST_P(DpfKeyGenerationTest, FailsIfBetaHasTheWrongSize) {
  EXPECT_THAT(
      dpf_->GenerateKeysIncremental(0, {1, 2}),
      StatusIs(absl::StatusCode::kInvalidArgument,
               "`beta` has to have the same size as `parameters` passed at "
               "construction"));
}

TEST_P(DpfKeyGenerationTest, FailsIfAlphaIsTooLarge) {
  if (log_domain_size_ >= 128) {
    // Alpha is an absl::uint128, so never too large in this case.
    return;
  }

  EXPECT_THAT(dpf_->GenerateKeys((absl::uint128{1} << log_domain_size_), 1),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       "`alpha` must be smaller than the output domain size"));
}

TEST_P(DpfKeyGenerationTest, FailsIfBetaIsTooLarge) {
  if (element_bitsize_ >= 128) {
    // Beta is an absl::uint128, so never too large in this case.
    return;
  }

  EXPECT_THAT(
      dpf_->GenerateKeys(0, (absl::uint128{1} << element_bitsize_)),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          "`beta[0]` larger than `parameters[0].element_bitsize()` allows"));
}

TEST_P(DpfKeyGenerationTest, FailsIfNumberOfCorrectionWordsDoesntMatch) {
  DpfKey key_a, key_b;

  DPF_ASSERT_OK_AND_ASSIGN(std::tie(key_a, key_b), dpf_->GenerateKeys(0, 0));
  key_a.add_correction_words();

  EXPECT_THAT(dpf_->CreateEvaluationContext(key_a),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       absl::StrCat("Malformed DpfKey: expected ",
                                    key_b.correction_words_size(),
                                    " correction words, but got ",
                                    key_a.correction_words_size())));
}

INSTANTIATE_TEST_SUITE_P(VaryDomainAndElementSizes, DpfKeyGenerationTest,
                         testing::Combine(testing::Values(0, 1, 2, 3, 4, 5, 6,
                                                          7, 8, 9, 10, 32, 63),
                                          testing::Values(8, 16, 32, 64, 128)));

struct DpfTestParameters {
  int log_domain_size;
  int element_bitsize;

  friend std::ostream& operator<<(std::ostream& os,
                                  const DpfTestParameters& parameters) {
    return os << "{ log_domain_size: " << parameters.log_domain_size
              << ", element_bitsize: " << parameters.element_bitsize << " }";
  }
};

class DpfEvaluationTest : public testing::TestWithParam<
                              std::tuple</*parameters*/
                                         std::vector<DpfTestParameters>,
                                         /*alpha*/ absl::uint128,
                                         /*beta*/ std::vector<absl::uint128>>> {
 protected:
  void SetUp() {
    const std::vector<DpfTestParameters>& parameters = std::get<0>(GetParam());
    parameters_.resize(parameters.size());
    for (int i = 0; i < static_cast<int>(parameters.size()); ++i) {
      parameters_[i].set_log_domain_size(parameters[i].log_domain_size);
      parameters_[i].set_element_bitsize(parameters[i].element_bitsize);
    }
    DPF_ASSERT_OK_AND_ASSIGN(
        dpf_, DistributedPointFunction::CreateIncremental(parameters_));
    alpha_ = std::get<1>(GetParam());
    beta_ = std::get<2>(GetParam());
    DPF_ASSERT_OK_AND_ASSIGN(keys_,
                             dpf_->GenerateKeysIncremental(alpha_, beta_));
  }

  template <typename T>
  void EvaluateAndCheck(EvaluationContext& ctx0, EvaluationContext& ctx1) {
    absl::StatusOr<std::vector<T>> result_0 = dpf_->EvaluateNext<T>({}, ctx0);
    absl::StatusOr<std::vector<T>> result_1 = dpf_->EvaluateNext<T>({}, ctx1);

    // Check results are ok.
    DPF_EXPECT_OK(result_0);
    DPF_EXPECT_OK(result_1);
    if (result_0.ok() && result_1.ok()) {
      EXPECT_EQ(result_0->size(), result_1->size());
      EXPECT_EQ(result_0->size(),
                uint64_t{1} << parameters_[0].log_domain_size());
      for (int64_t i = 0; i < static_cast<int64_t>(result_0->size()); ++i) {
        if (i == static_cast<int64_t>(alpha_)) {
          // We need to static_cast here since otherwise operator+ returns an
          // unsigned int without doing a modular reduction, which causes the
          // test to fail on types with sizeof(T) < sizeof(unsigned).
          EXPECT_EQ(static_cast<T>((*result_0)[i] + (*result_1)[i]), beta_[0]);
        } else {
          EXPECT_EQ(static_cast<T>((*result_0)[i] + (*result_1)[i]), 0);
        }
      }
    }
  }

  std::vector<DpfParameters> parameters_;
  std::unique_ptr<DistributedPointFunction> dpf_;
  absl::uint128 alpha_;
  std::vector<absl::uint128> beta_;
  std::pair<DpfKey, DpfKey> keys_;
};

TEST_P(DpfEvaluationTest, FailsIfOutputCorrectionIsMissing) {
  if (parameters_.size() == 1) {
    // Only one hierarchy level -> No output correction in correction words.
    return;
  }
  int tree_level;

  for (tree_level = 0; tree_level < keys_.first.correction_words_size();
       ++tree_level) {
    CorrectionWord* word = keys_.first.mutable_correction_words(tree_level);
    // Remove first output correction word.
    if (word->has_output()) {
      word->clear_output();
      break;
    }
  }

  EXPECT_THAT(dpf_->CreateEvaluationContext(keys_.first),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       StartsWith(absl::StrCat(
                           "Malformed DpfKey: expected correction_words[",
                           tree_level, "] to contain the output correction"))));
}

TEST_P(DpfEvaluationTest, FailsIfParameterSizeDoesntMatch) {
  DPF_ASSERT_OK_AND_ASSIGN(EvaluationContext ctx,
                           dpf_->CreateEvaluationContext(keys_.first));

  ctx.mutable_parameters()->erase(ctx.parameters().end() - 1);

  EXPECT_THAT(dpf_->EvaluateNext<absl::uint128>({}, ctx),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       "Number of parameters in `ctx` doesn't match"));
}

TEST_P(DpfEvaluationTest, FailsIfParameterDoesntMatch) {
  DPF_ASSERT_OK_AND_ASSIGN(EvaluationContext ctx,
                           dpf_->CreateEvaluationContext(keys_.first));

  ctx.mutable_parameters(0)->set_log_domain_size(
      ctx.parameters(0).log_domain_size() + 1);

  EXPECT_THAT(dpf_->EvaluateNext<absl::uint128>({}, ctx),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       "Parameter 0 in `ctx` doesn't match"));
}

TEST_P(DpfEvaluationTest, FailsIfContextFullyEvaluated) {
  DPF_ASSERT_OK_AND_ASSIGN(EvaluationContext ctx,
                           dpf_->CreateEvaluationContext(keys_.first));

  ctx.set_hierarchy_level(parameters_.size());

  EXPECT_THAT(dpf_->EvaluateNext<absl::uint128>({}, ctx),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       "This context has already been fully evaluated"));
}

INSTANTIATE_TEST_SUITE_P(
    TwoHierarchyLevels, DpfEvaluationTest,
    testing::Combine(
        // DPF parameters.
        testing::Values(
            // Equal element sizes.
            std::vector<DpfTestParameters>{
                {.log_domain_size = 5, .element_bitsize = 8},
                {.log_domain_size = 10, .element_bitsize = 8}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 5, .element_bitsize = 16},
                {.log_domain_size = 10, .element_bitsize = 16}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 5, .element_bitsize = 32},
                {.log_domain_size = 10, .element_bitsize = 32}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 5, .element_bitsize = 64},
                {.log_domain_size = 10, .element_bitsize = 64}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 5, .element_bitsize = 128},
                {.log_domain_size = 10, .element_bitsize = 128}},
            // First correction is in seed word.
            std::vector<DpfTestParameters>{
                {.log_domain_size = 0, .element_bitsize = 8},
                {.log_domain_size = 10, .element_bitsize = 128}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 0, .element_bitsize = 16},
                {.log_domain_size = 10, .element_bitsize = 128}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 0, .element_bitsize = 32},
                {.log_domain_size = 10, .element_bitsize = 128}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 0, .element_bitsize = 64},
                {.log_domain_size = 10, .element_bitsize = 128}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 0, .element_bitsize = 128},
                {.log_domain_size = 10, .element_bitsize = 128}}),
        testing::Values(0, 1, 2, 100, 1023),  // alpha
        testing::Values(std::vector<absl::uint128>({1, 2}),
                        std::vector<absl::uint128>({80, 90}),
                        std::vector<absl::uint128>({255, 255}))));  // beta

INSTANTIATE_TEST_SUITE_P(
    ThreeHierarchyLevels, DpfEvaluationTest,
    testing::Combine(
        // DPF parameters.
        testing::Values<std::vector<DpfTestParameters>>(
            // Equal element sizes.
            std::vector<DpfTestParameters>{
                {.log_domain_size = 10, .element_bitsize = 8},
                {.log_domain_size = 20, .element_bitsize = 8},
                {.log_domain_size = 30, .element_bitsize = 8}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 10, .element_bitsize = 16},
                {.log_domain_size = 20, .element_bitsize = 16},
                {.log_domain_size = 30, .element_bitsize = 16}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 10, .element_bitsize = 32},
                {.log_domain_size = 20, .element_bitsize = 32},
                {.log_domain_size = 30, .element_bitsize = 32}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 10, .element_bitsize = 64},
                {.log_domain_size = 20, .element_bitsize = 64},
                {.log_domain_size = 30, .element_bitsize = 64}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 10, .element_bitsize = 128},
                {.log_domain_size = 20, .element_bitsize = 128},
                {.log_domain_size = 30, .element_bitsize = 128}},
            // Small level distances.
            std::vector<DpfTestParameters>{
                {.log_domain_size = 4, .element_bitsize = 8},
                {.log_domain_size = 5, .element_bitsize = 8},
                {.log_domain_size = 6, .element_bitsize = 8}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 3, .element_bitsize = 16},
                {.log_domain_size = 4, .element_bitsize = 16},
                {.log_domain_size = 5, .element_bitsize = 16}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 2, .element_bitsize = 32},
                {.log_domain_size = 3, .element_bitsize = 32},
                {.log_domain_size = 4, .element_bitsize = 32}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 1, .element_bitsize = 64},
                {.log_domain_size = 2, .element_bitsize = 64},
                {.log_domain_size = 3, .element_bitsize = 64}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 0, .element_bitsize = 128},
                {.log_domain_size = 1, .element_bitsize = 128},
                {.log_domain_size = 2, .element_bitsize = 128}}),
        testing::Values(0, 1, 2),  // alpha
        testing::Values(std::vector<absl::uint128>({1, 2, 3}),
                        std::vector<absl::uint128>({80, 90, 100}),
                        std::vector<absl::uint128>({255, 255, 255}))));  // beta

class DpfEvaluationTest2 : public DpfEvaluationTest {};

TEST_P(DpfEvaluationTest2, TestCorrectness) {
  DPF_ASSERT_OK_AND_ASSIGN(EvaluationContext ctx0,
                           dpf_->CreateEvaluationContext(keys_.first));
  DPF_ASSERT_OK_AND_ASSIGN(EvaluationContext ctx1,
                           dpf_->CreateEvaluationContext(keys_.second));
  ASSERT_EQ(parameters_.size(), 1) << "We only support regular DPF evaluation!";

  switch (parameters_[0].element_bitsize()) {
    case 8:
      EvaluateAndCheck<uint8_t>(ctx0, ctx1);
      break;
    case 16:
      EvaluateAndCheck<uint16_t>(ctx0, ctx1);
      break;
    case 32:
      EvaluateAndCheck<uint32_t>(ctx0, ctx1);
      break;
    case 64:
      EvaluateAndCheck<uint64_t>(ctx0, ctx1);
      break;
    case 128:
      EvaluateAndCheck<absl::uint128>(ctx0, ctx1);
      break;
    default:
      ASSERT_TRUE(0) << "Unsupported element_bitsize";
  }
}

INSTANTIATE_TEST_SUITE_P(
    OneHierarchyLevelVaryElementSizes, DpfEvaluationTest2,
    testing::Combine(
        // DPF parameters.
        testing::Values(
            // Vary element sizes, small domain size.
            std::vector<DpfTestParameters>{
                {.log_domain_size = 4, .element_bitsize = 8}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 4, .element_bitsize = 16}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 4, .element_bitsize = 32}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 4, .element_bitsize = 64}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 4, .element_bitsize = 128}},
            // Vary element sizes, medium domain size.
            std::vector<DpfTestParameters>{
                {.log_domain_size = 10, .element_bitsize = 8}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 10, .element_bitsize = 16}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 10, .element_bitsize = 32}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 10, .element_bitsize = 64}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 10, .element_bitsize = 128}}),
        testing::Values(0, 1, 15),  // alpha
        testing::Values(std::vector<absl::uint128>(1, 1),
                        std::vector<absl::uint128>(1, 100),
                        std::vector<absl::uint128>(1, 255))));  // beta

INSTANTIATE_TEST_SUITE_P(
    OneHierarchyLevelVaryDomainSizes, DpfEvaluationTest2,
    testing::Combine(
        // DPF parameters.
        testing::Values(
            // Vary domain sizes, small element size.
            std::vector<DpfTestParameters>{
                {.log_domain_size = 0, .element_bitsize = 8}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 1, .element_bitsize = 8}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 2, .element_bitsize = 8}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 3, .element_bitsize = 8}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 4, .element_bitsize = 8}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 5, .element_bitsize = 8}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 6, .element_bitsize = 8}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 7, .element_bitsize = 8}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 8, .element_bitsize = 8}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 9, .element_bitsize = 8}},
            // Vary domain sizes, medium element size.
            std::vector<DpfTestParameters>{
                {.log_domain_size = 0, .element_bitsize = 64}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 1, .element_bitsize = 64}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 2, .element_bitsize = 64}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 3, .element_bitsize = 64}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 4, .element_bitsize = 64}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 5, .element_bitsize = 64}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 6, .element_bitsize = 64}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 7, .element_bitsize = 64}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 8, .element_bitsize = 64}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 9, .element_bitsize = 64}},
            // Vary domain sizes, large element size.
            std::vector<DpfTestParameters>{
                {.log_domain_size = 0, .element_bitsize = 128}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 1, .element_bitsize = 128}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 2, .element_bitsize = 128}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 3, .element_bitsize = 128}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 4, .element_bitsize = 128}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 5, .element_bitsize = 128}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 6, .element_bitsize = 128}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 7, .element_bitsize = 128}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 8, .element_bitsize = 128}},
            std::vector<DpfTestParameters>{
                {.log_domain_size = 9, .element_bitsize = 128}}),
        testing::Values(0),  // alpha
        testing::Values(std::vector<absl::uint128>(1, 1),
                        std::vector<absl::uint128>(1, 100),
                        std::vector<absl::uint128>(1, 255))));  // beta

}  // namespace
}  // namespace dpf
}  // namespace private_statistics