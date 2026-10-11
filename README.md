# Plucked Physical Modelling Engine

An open-source, real-time physical-modelling engine for the piano and plucked strings — concert grand and upright piano first, then harpsichord, guitars, tanpura, sitar, kantele, shamisen and tanbur — delivered as CLAP, VST3, AUv2 and LV2 plug-ins and a standalone app for Windows, macOS and Linux.

Every sound comes from simulated physics: modal stiff strings with two polarisations and longitudinal waves, a nonlinear felt hammer with una corda, dampers and pedals, a soundboard computed from its wood, thickness and ribs, and explicit energy-stable contact for frets, jawari and sawari bridges. Materials and dimensions are user-editable parameters.

## Status

Planning is complete; implementation has not started. Work proceeds phase by phase under an AI orchestrator: planning documentation is written by Opus agents and code by Sonnet agents.

| Start here | What it is |
| --- | --- |
| [CLAUDE.md](CLAUDE.md) | Orchestrator instructions and model policy |
| [docs/plan/phasing-plan.md](docs/plan/phasing-plan.md) | Phases, gates, guidelines, decisions |
| [docs/plan/engine-specification.md](docs/plan/engine-specification.md) | Normative engine specification |
| [docs/plan/implementation-plan.md](docs/plan/implementation-plan.md) | Architecture and technology background |
| [docs/plan/literature-survey.md](docs/plan/literature-survey.md) | Research survey |
| [docs/status/phase-status.md](docs/status/phase-status.md) | Current progress |

## Building

CMake presets and the Python toolchain are described in [docs/dev/build.md](docs/dev/build.md); contribution rules are in [CONTRIBUTING.md](CONTRIBUTING.md).

## Licence

MIT — see [LICENSE](LICENSE).
