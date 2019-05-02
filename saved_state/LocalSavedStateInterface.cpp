/* Copyright 2017-present Facebook, Inc.
 * Licensed under the Apache License, Version 2.0 */

#include "LocalSavedStateInterface.h"
#include "watchman.h"
#include "watchman_cmd.h"

static const int kDefaultMaxCommits{10};

W_CAP_REG("saved-state-local")

namespace watchman {

LocalSavedStateInterface::LocalSavedStateInterface(
    const json_ref& savedStateConfig,
    const SCM* scm)
    : SavedStateInterface(), scm_(scm) {
  // Max commits to search in source control history for a saved state
  auto maxCommits = savedStateConfig.get_default("max-commits");
  if (maxCommits) {
    if (!maxCommits.isInt()) {
      throw QueryParseError("'max-commits' must be an integer");
    }
    maxCommits_ = json_integer_value(maxCommits);
    if (maxCommits_ < 1) {
      throw QueryParseError("'max-commits' must be a positive integer");
    }
  } else {
    maxCommits_ = kDefaultMaxCommits;
  }
  // Local path to search for saved states. This path will only ever be read,
  // never written.
  auto localStoragePath = savedStateConfig.get_default("local-storage-path");
  if (!localStoragePath) {
    throw QueryParseError(
        "'local-storage-path' must be present in saved state config");
  }
  if (!localStoragePath.isString()) {
    throw QueryParseError("'local-storage-path' must be a string");
  }
  localStoragePath_ = json_to_w_string(localStoragePath);
  if (!w_string_path_is_absolute(localStoragePath_)) {
    throw QueryParseError("'local-storage-path' must be an absolute path");
  }
  // The saved state project, which must be a sub-directory in the local storage
  // path.
  auto project = savedStateConfig.get_default("project");
  if (!project) {
    throw QueryParseError("'project' must be present in saved state config");
  }
  if (!project.isString()) {
    throw QueryParseError("'project' must be a string");
  }
  project_ = json_to_w_string(project);
  if (w_string_path_is_absolute(project_)) {
    throw QueryParseError("'project' must be a relative path");
  }
}

SavedStateInterface::SavedStateResult
LocalSavedStateInterface::getMostRecentSavedStateImpl(
    w_string_piece lookupCommitId) const {
  auto commitIds =
      scm_->getCommitsPriorToAndIncluding(lookupCommitId, maxCommits_);
  for (auto& commitId : commitIds) {
    auto path = w_string::pathCat({localStoragePath_, project_, commitId});
    // We could return a path that no longer exists if the path is removed
    // (for example by saved state GC) after we check that the path exists
    // here, but before the client reads the state. We've explicitly chosen to
    // return the state without additional safety guarantees, and leave it to
    // the client to ensure GC happens only after states are no longer likely
    // to be used.
    if (w_path_exists(path.c_str())) {
      log(DBG, "Found saved state for commit ", commitId, "\n");
      SavedStateInterface::SavedStateResult result;
      result.commitId = commitId;
      result.savedStateInfo =
          json_object({{"local-path", w_string_to_json(path)},
                       {"commit-id", w_string_to_json(commitId)}});
      return result;
    }
  }
  SavedStateInterface::SavedStateResult result;
  result.commitId = w_string();
  result.savedStateInfo = json_object(
      {{"error", w_string_to_json("No suitable saved state found")}});
  return result;
}
} // namespace watchman
