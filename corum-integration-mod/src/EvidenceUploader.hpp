#pragma once

#include <functional>
#include <string>

namespace corum {

struct EvidencePreparationResult {
    bool success = false;
    std::string evidenceID;
    std::string error;
};

using EvidencePreparationCallback =
    std::function<void(EvidencePreparationResult)>;

std::string latestEvidenceID(int levelID);

bool hasPendingEvidenceForSubmission(
    int canonicalLevelID,
    int accountID
);

void prepareEvidenceForSubmission(
    int canonicalLevelID,
    int sourceLevelID,
    int accountID,
    EvidencePreparationCallback callback
);

void markEvidenceSubmissionComplete(
    int canonicalLevelID,
    int accountID
);

} // namespace corum
