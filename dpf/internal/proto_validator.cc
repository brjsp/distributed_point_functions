#include "dpf/internal/proto_validator.h"

#include "dpf/status_macros.h"

namespace distributed_point_functions::dpf_internal {

ProtoValidator::ProtoValidator(std::vector<DpfParameters> parameters,
                               int tree_levels_needed,
                               absl::flat_hash_map<int, int> tree_to_hierarchy,
                               std::vector<int> hierarchy_to_tree)
    : parameters_(std::move(parameters)),
      tree_levels_needed_(tree_levels_needed),
      tree_to_hierarchy_(std::move(tree_to_hierarchy)),
      hierarchy_to_tree_(std::move(hierarchy_to_tree)) {}

absl::StatusOr<std::unique_ptr<ProtoValidator>> ProtoValidator::Create(
    absl::Span<const DpfParameters> parameters) {
  DPF_RETURN_IF_ERROR(ValidateParameters(parameters));

  // Map hierarchy levels to levels in the evaluation tree for value correction,
  // and vice versa.
  absl::flat_hash_map<int, int> tree_to_hierarchy;
  std::vector<int> hierarchy_to_tree(parameters.size());
  // Also keep track of the height needed for the evaluation tree so far.
  int tree_levels_needed = 0;
  for (int i = 0; i < static_cast<int>(parameters.size()); ++i) {
    int log_element_size =
        static_cast<int>(std::log2(parameters[i].element_bitsize()));
    // The tree level depends on the domain size and the element size. A single
    // AES block can fit 128 = 2^7 bits, so usually tree_level ==
    // log_domain_size iff log_element_size == 7. For smaller element sizes, we
    // can reduce the tree_level (and thus the height of the tree) by the
    // difference between log_element_size and 7. However, since the minimum
    // tree level is 0, we have to ensure that no two hierarchy levels map to
    // the same tree_level, hence the std::max.
    int tree_level =
        std::max(tree_levels_needed,
                 parameters[i].log_domain_size() - 7 + log_element_size);
    tree_to_hierarchy[tree_level] = i;
    hierarchy_to_tree[i] = tree_level;
    tree_levels_needed = std::max(tree_levels_needed, tree_level + 1);
  }

  return absl::WrapUnique(new ProtoValidator(
      std::vector<DpfParameters>(parameters.begin(), parameters.end()),
      tree_levels_needed, std::move(tree_to_hierarchy),
      std::move(hierarchy_to_tree)));
}

absl::Status ProtoValidator::ValidateParameters(
    absl::Span<const DpfParameters> parameters) {
  // Check that parameters are valid.
  if (parameters.empty()) {
    return absl::InvalidArgumentError("`parameters` must not be empty");
  }
  // Sentinel values for checking that domain sizes are increasing and not too
  // far apart, and element sizes are non-decreasing.
  int previous_log_domain_size = 0;
  int previous_element_bitsize = 1;
  for (int i = 0; i < static_cast<int>(parameters.size()); ++i) {
    // Check log_domain_size.
    int32_t log_domain_size = parameters[i].log_domain_size();
    if (log_domain_size < 0) {
      return absl::InvalidArgumentError(
          "`log_domain_size` must be non-negative");
    }
    if (i > 0 && log_domain_size <= previous_log_domain_size) {
      return absl::InvalidArgumentError(
          "`log_domain_size` fields must be in ascending order in "
          "`parameters`");
    }
    // For full evaluation of a particular hierarchy level, want to be able to
    // represent 1 << (log_domain_size - previous_log_domain_size) in an
    // int64_t, so hierarchy levels may be at most 62 apart. Note that such
    // large gaps between levels are rare in practice, and in any case this
    // error can circumvented by adding additional intermediate hierarchy
    // levels.
    if (log_domain_size > previous_log_domain_size + 62) {
      return absl::InvalidArgumentError(
          "Hierarchies may be at most 62 levels apart");
    }
    previous_log_domain_size = log_domain_size;

    // Check element_bitsize.
    int32_t element_bitsize = parameters[i].element_bitsize();
    if (element_bitsize < 1) {
      return absl::InvalidArgumentError("`element_bitsize` must be positive");
    }
    if (element_bitsize > 128) {
      return absl::InvalidArgumentError(
          "`element_bitsize` must be less than or equal to 128");
    }
    if ((element_bitsize & (element_bitsize - 1)) != 0) {
      return absl::InvalidArgumentError(
          "`element_bitsize` must be a power of 2");
    }
    if (element_bitsize < previous_element_bitsize) {
      return absl::InvalidArgumentError(
          "`element_bitsize` fields must be non-decreasing in "
          "`parameters`");
    }
    previous_element_bitsize = element_bitsize;
  }
  return absl::OkStatus();
}

absl::Status ProtoValidator::ValidateDpfKey(const DpfKey& key) const {
  // Check that `key` has the seed and last_level_output_correction set.
  if (!key.has_seed()) {
    return absl::InvalidArgumentError("key.seed must be present");
  }
  if (!key.has_last_level_output_correction()) {
    return absl::InvalidArgumentError(
        "key.last_level_output_correction must be present");
  }
  // Check that `key` is valid for the DPF defined by `parameters_`.
  if (key.correction_words_size() != tree_levels_needed_ - 1) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Malformed DpfKey: expected ", tree_levels_needed_ - 1,
        " correction words, but got ", key.correction_words_size()));
  }
  for (int i = 0; i < static_cast<int>(hierarchy_to_tree_.size()); ++i) {
    if (hierarchy_to_tree_[i] == tree_levels_needed_ - 1) {
      // The output correction of the last tree level is always stored in
      // last_level_output_correction.
      continue;
    }
    if (!key.correction_words(hierarchy_to_tree_[i]).has_output()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Malformed DpfKey: expected correction_words[", hierarchy_to_tree_[i],
          "] to contain the output correction of hierarchy level ", i));
    }
  }
  return absl::OkStatus();
}

absl::Status ProtoValidator::ValidateEvaluationContext(
    const EvaluationContext& ctx) const {
  if (ctx.parameters_size() != static_cast<int>(parameters_.size())) {
    return absl::InvalidArgumentError(
        "Number of parameters in `ctx` doesn't match");
  }
  for (int i = 0; i < ctx.parameters_size(); ++i) {
    if (ctx.parameters(i).log_domain_size() !=
            parameters_[i].log_domain_size() ||
        ctx.parameters(i).element_bitsize() !=
            parameters_[i].element_bitsize()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Parameter ", i, " in `ctx` doesn't match"));
    }
  }
  if (!ctx.has_key()) {
    return absl::InvalidArgumentError("ctx.key must be present");
  }
  DPF_RETURN_IF_ERROR(ValidateDpfKey(ctx.key()));
  if (ctx.previous_hierarchy_level() >= ctx.parameters_size() - 1) {
    return absl::InvalidArgumentError(
        "This context has already been fully evaluated");
  }
  if (!ctx.partial_evaluations().empty() &&
      ctx.partial_evaluations_level() >= ctx.previous_hierarchy_level()) {
    return absl::InvalidArgumentError(
        "ctx.previous_hierarchy_level must be less than ctx.hierarchy_level");
  }
  return absl::OkStatus();
}

}  // namespace distributed_point_functions::dpf_internal