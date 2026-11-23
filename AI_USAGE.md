<!-- SPDX-FileCopyrightText: 2026 Universidad Politécnica de Madrid (UPM) - CVAR-UPM -->
<!-- SPDX-License-Identifier: BSD-3-Clause -->

# AI-assisted development disclosure — Block Plugin Engine (BPE)

**Release:** 1.0.0
**Institution and research group:** Universidad Politécnica de Madrid (UPM), CVAR-UPM
**Human author, lead designer and maintainer:** Santiago Tapia-Fernández (santiago.tapia@upm.es; ORCID: https://orcid.org/0000-0002-1418-9276)

## 1. Human-led architecture and technical decisions

**BPE's architecture and principal design decisions were led by the human author.**
The core concepts, engineering requirements, interface boundaries, design constraints,
and many of the simplifications and improvements adopted during development originated
from Santiago Tapia-Fernández. These decisions were not delegated to an AI system.

In technical discussions, the author frequently asked the assistant to propose an
approach **as an independent design cross-check**. The author often already had a
preferred solution and used the exchange to examine whether the assistant could reach
a compatible conclusion, identify trade-offs or expose an overlooked issue. Agreement
was not treated as evidence of correctness, nor was the assistant expected to endorse
the author's suggestion automatically. The author actively challenged proposals and
introduced alternative designs, often simplifying the architecture or improving its
quality and maintainability. The resulting solution was selected through human
engineering judgement and validation, not by default acceptance of AI output.

This distinction matters: extensive AI assistance in writing and revising source code
does **not** imply that the framework's design was generated or autonomously decided
by AI. The author retains responsibility for the design and engineering decisions.

## 2. AI assistance and its scope

OpenAI ChatGPT was used as an interactive engineering assistant for assessing design
alternatives, explaining technical trade-offs, preparing C++/Python/CMake/YAML patches,
reviewing implementations, diagnosing compilation and runtime failures, proposing and
refining tests, and drafting documentation. Examples include scheduler and lifecycle
refactoring, Blackboard concurrency tests, ROS boundary integration, example programs,
and release preparation.

Parts of the implementation and documentation were produced with AI assistance, but
not accepted solely because they were generated. The exact AI-produced share of the
codebase has not been measured. No file-by-file or line-by-line authorship percentage
is asserted.

BPE incorporates earlier work by the author, including LFBB, rather than being wholly
created from AI-generated output.

## 3. Iterative development and verification process

The work followed a human-directed, incremental development cycle:

1. **Problem definition:** the author specified requirements, constraints and desired
   system behaviour, including real-time and concurrent execution properties.
2. **Design exploration and challenge:** the author and assistant compared technical
   alternatives. The author scrutinized recommendations and frequently proposed
   simpler or more suitable designs; the assistant provided counterarguments or
   identified implications where appropriate.
3. **Implementation:** the assistant often prepared small source patches reflecting
   the selected design. The author decided whether and when to integrate them.
4. **Human review and real-environment tests:** the author applied patches, compiled
   and ran the code in the ROS 2 environment, and inspected behaviour and test output.
5. **Diagnosis and refinement:** compiler diagnostics, runtime logs, failing tests,
   race conditions and regression results were used to revise implementation details.
   Neither first-pass AI output nor agreement between participants was considered
   proof of correctness.
6. **Regression and integration:** unit, lifecycle, plugin-loading, real/simulated
   scheduler, ROS boundary, and concurrent Blackboard tests were added or expanded.
   Representative examples were also executed in the actual environment.
7. **Release review:** API compatibility, installation, licensing, attribution and
   documentation were reviewed before the 1.0.0 release.

## 4. AI systems and model identification

The primary assistant was **OpenAI ChatGPT**. The model used in the final release
preparation discussion was **GPT-6 (November 2026)**. Earlier discussions and
implementation steps may have used different OpenAI models; an exact historical
model-to-commit mapping is not available. This document does not attribute the
entire repository to GPT-6 or claim a uniform model version throughout development.

## 5. Accountability and limits

The author and maintainers retain responsibility for architectural choices, code
acceptance, provenance review, licensing, tests, integration and release decisions.
AI is not named as an author, copyright holder or maintainer. AI assistance does not
constitute an independent formal verification or security certification. Passing
functional and concurrency tests does not guarantee hard real-time behaviour on
arbitrary hardware, workloads or operating systems.

## 6. Transparency and future contributions

The version-control history records changes and their review context where available.
This project does not claim a precise fraction of AI-authored lines, and this disclosure
should not be mistaken for an SPDX license statement or software bill of materials.

Contributors using AI tools should disclose substantial assistance in review materials,
verify generated code and dependencies, and validate correctness, concurrency and
licensing. Human ownership of decisions and testing remains essential.

License and copyright metadata are provided separately under the SPDX and REUSE
conventions: https://reuse.software/spec/.
