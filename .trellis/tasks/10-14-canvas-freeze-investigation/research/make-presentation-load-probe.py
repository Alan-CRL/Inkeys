"""Extract original controller/Host fragments for a no-window file sharing probe.

Does not compile or execute anything.  Service/UInk implementations are linked
from the already-built product objects; none are replaced by test doubles.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

RESEARCH = Path(__file__).resolve().parent
ROOT = RESEARCH.parents[3]
DRAW = ROOT / "Inkeys/Inkeys/Drawing/Draw3"


def between(source: str, start: str, end: str) -> str:
    begin = source.index(start)
    finish = source.index(end, begin)
    return source[begin:finish]


controller_path = DRAW / "Draw3.DrawingController.cpp"
host_path = DRAW / "Draw3.Host.cpp"
controller = controller_path.read_text(encoding="utf-8-sig")
host = host_path.read_text(encoding="utf-8-sig")

gate = between(
    controller,
    "\t\tbool PresentationTrackMatchesMode(",
    "\n\t\ttemplate <typename Capture, typename Acknowledge>",
)
current_completion = controller[controller.index(
    "\t\t\t\t\tconst Bridge::PresentationTarget& latestTarget = completionIsActive &&"):]
failure_branch = between(
    current_completion,
    "\t\t\t\t\t\tactivePresentationLoadPending = false;",
    "\t\t\t\t\t\tif (loadedVerified)\n",
)
host_condition = between(
    host,
    "\t\t\tif (state.workspace == Bridge::Workspace::Presentation &&\n"
    "\t\t\t\tstate.presentationTarget &&\n",
    "\n\t\t\t{",
)

fragments = {"gate": gate, "completion_failure_branch": failure_branch,
             "host_target_condition": host_condition}
provenance = {
    "controller": {"path": str(controller_path),
                   "sha256": hashlib.sha256(controller_path.read_bytes()).hexdigest()},
    "host": {"path": str(host_path),
             "sha256": hashlib.sha256(host_path.read_bytes()).hexdigest()},
    "fragments": {},
}
for name, fragment in fragments.items():
    source = host if name == "host_target_condition" else controller
    provenance["fragments"][name] = {
        "line": source[:source.index(fragment)].count("\n") + 1,
        "sha256": hashlib.sha256(fragment.encode()).hexdigest(),
    }

generated = """// Generated verbatim fragments; regenerate using make-presentation-load-probe.py.
#pragma once
namespace ProbeOriginal
{
    using namespace Inkeys::Drawing::Draw3;
""" + gate + """
    // 此 do/while 仅给原分支的 continue 提供作用域；生产分支内容没有改写。
    inline void ApplyOriginalCurrentLoadFailure(
        const PresentationPersistenceCompletion& completion,
        bool& activePresentationLoadPending,
        bool& activePresentationPersistenceInitialized,
        const std::optional<Bridge::PresentationTarget>& activePresentationTarget,
        std::uint64_t activePresentationSlotGeneration,
        PresentationCurrentLoadRetry& currentLoadRetry)
    {
        const bool loadedVerified = PresentationLoadedForLane(completion);
        const bool emptyVerified = PresentationEmptyLaneVerified(completion);
        do
        {
""" + failure_branch + """
        } while (false);
    }

    inline bool OriginalHostWouldQueueTarget(
        const Bridge::ProductState& state,
        std::uint64_t requestedPresentationTargetRevision,
        bool restoreLatestScene)
    {
""" + host_condition + """
            return true;
        return false;
    }
}
"""

(RESEARCH / "presentation-load-gate.generated.hpp").write_text(generated, encoding="utf-8")
(RESEARCH / "presentation-load-probe-source.json").write_text(
    json.dumps(provenance, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print("Generated presentation-load-gate.generated.hpp and source hashes; no compilation/execution.")
