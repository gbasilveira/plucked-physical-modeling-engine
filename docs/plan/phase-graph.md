# Phase dependency graph

Generated from [phases.yaml](phases.yaml) by `docs/plan/tools/gen_phase_graph.py`. Do not edit by hand.

Solid arrows: phase or gate dependencies. Dotted arrows: human decisions (see the Decisions section of [phasing-plan.md](phasing-plan.md)). Hexagons are gates.

```mermaid
flowchart LR
  subgraph T_0["Track 0: Foundations"]
    P0_1["P0.1 Repository and build skeleton"]
    P0_2["P0.2 CI and quality gates"]
    P0_3["P0.3 Test and analysis framework"]
    P0_4["P0.4 Interface skeleton"]
    P0_5["P0.5 Reference solver"]
  end
  subgraph T_A["Track A: Core numerics"]
    A1["A1 Modal oscillator"]
    A2["A2 Stiff string compiler"]
    A3["A3 SAV framework and low-rank solve"]
    A4["A4 Bridge coupling"]
    A5["A5 Hammer and felt"]
    A6["A6 Dampers and pedals"]
    A7["A7 Geometric nonlinearity and longitudinal waves"]
    A8["A8 SIMD kernels"]
    A9["A9 Implicit bridge step and bridge numerics options"]
    A10["A10 Geometrically exact string tier"]
  end
  subgraph T_B["Track B: Bodies and offline toolchain"]
    B1["B1 Instrument description schema and loader"]
    B2["B2 Plate and body FEM compiler"]
    B3["B3 Radiation, mobility and local bridge builder"]
    B4["B4 Modal data import"]
    B5["B5 Parameter fitting toolkit"]
    B6["B6 Instrument-file prestress, moisture reference and shank"]
  end
  subgraph T_C["Track C: Engine runtime"]
    C1["C1 Note group runtime"]
    C2["C2 Global body engine"]
    C3["C3 Sympathetic path and guard"]
    C4["C4 Event and parameter system"]
    C5["C5 Scheduler and threading"]
    C6["C6 Sleep, wake and state management"]
    C7["C7 Reference mode"]
    C8["C8 Offline render CLI and Python bindings"]
  end
  subgraph T_P["Track P: Plucked-string numerics"]
    P1["P1 Plucked exciters"]
    P2["P2 Distributed barriers"]
    P3["P3 Frets and stopping finger"]
    P4["P4 Jawari, thread and sawari"]
    P5["P5 Bending and time-varying tension"]
    P6["P6 Plucked-instrument integration"]
  end
  subgraph T_D["Track D: Plug-in shell and application"]
    D1["D1 CLAP plug-in shell"]
    D2["D2 VST3, AUv2 and standalone"]
    D3["D3 LV2 plug-in"]
    D4["D4 GUI"]
    D5["D5 Instrument and preset management"]
    D6["D6 Real-engine integration"]
  end
  subgraph T_E["Track E: Instrument content"]
    E1["E1 Concert grand piano"]
    E2["E2 Piano measurement and fitting"]
    E3["E3 Upright piano"]
    E4["E4 Harpsichord"]
    E5["E5 Guitars"]
    E6["E6 Tanpura"]
    E7["E7 Sitar"]
    E8["E8 Kantele, shamisen and tanbur"]
  end
  subgraph T_V["Track V: Validation and quality"]
    V1["V1 Traceability and oracle suite"]
    V2["V2 Performance and real-time validation"]
    V3["V3 Sympathetic approximation validation"]
    V4["V4 Listening and playability tests"]
    V5["V5 Robustness - fuzzing and soak"]
    V6["V6 Host compatibility matrix"]
  end
  subgraph T_H["Track H: Release and publishing"]
    H1["H1 Licensing, provenance and compliance"]
    H2["H2 Packaging and signing"]
    H3["H3 Documentation and website"]
    H4["H4 Release pipeline"]
    H5["H5 Public release 1.0 (pianos)"]
    H6["H6 Plucked-instrument releases (1.x)"]
  end
  G0{{"G0 Foundations"}}
  G1{{"G1 Core numerics"}}
  G2{{"G2 Piano engine"}}
  G3{{"G3 Plug-in alpha"}}
  G4{{"G4 Piano content"}}
  G5{{"G5 Release candidate"}}
  G6{{"G6 Plucked numerics"}}
  G7{{"G7 Plucked instrument (one gate per instrument)"}}
  D_1[/"D-1 GUI technology and project licence"/]
  D_10[/"D-10 Beta programme"/]
  D_11[/"D-11 Human approver for G5 and each G7"/]
  D_12[/"D-12 Redistribution rights for published piano data"/]
  D_2[/"D-2 Repository host and organisation name"/]
  D_3[/"D-3 Brand, product name and frozen plug-in identifiers"/]
  D_4[/"D-4 Reference machines for performance validation"/]
  D_5[/"D-5 Access to a concert grand and measurement equipment"/]
  D_6[/"D-6 Listening-test participants and consent process"/]
  D_7[/"D-7 Host licences for compatibility testing"/]
  D_8[/"D-8 Prior-art and patent review sign-off"/]
  D_9[/"D-9 Signing identities (Apple Developer ID, Windows code signing) as CI secrets"/]
  D_2 -.-> P0_1
  P0_1 --> P0_2
  P0_1 --> P0_3
  P0_1 --> P0_4
  P0_3 --> P0_5
  G0 --> A1
  A1 --> A2
  A1 --> A3
  A2 --> A4
  A2 --> A5
  A3 --> A5
  A4 --> A5
  A2 --> A6
  A3 --> A6
  A3 --> A7
  A4 --> A7
  A1 --> A8
  A2 --> A8
  A4 --> A8
  A4 --> A9
  A7 --> A10
  G0 --> B1
  B1 --> B2
  B2 --> B3
  B1 --> B4
  B1 --> B5
  A5 --> B5
  C8 --> B5
  B1 --> B6
  B2 --> B6
  G1 --> C1
  B1 --> C1
  G1 --> C2
  B1 --> C2
  C1 --> C3
  C2 --> C3
  C7 --> C3
  C1 --> C4
  C1 --> C5
  C2 --> C5
  C3 --> C6
  C1 --> C7
  C2 --> C7
  C4 --> C8
  G1 --> P1
  G1 --> P2
  P1 --> P3
  P2 --> P3
  P2 --> P4
  P1 --> P5
  G1 --> P5
  P1 --> P6
  P2 --> P6
  P3 --> P6
  P4 --> P6
  P5 --> P6
  C1 --> P6
  C4 --> P6
  G0 --> D1
  D1 --> D2
  D_3 -.-> D2
  D1 --> D3
  D1 --> D4
  C4 --> D4
  D_1 -.-> D4
  D1 --> D5
  B1 --> D5
  G2 --> D6
  D1 --> D6
  D2 --> D6
  D3 --> D6
  D4 --> D6
  D5 --> D6
  G2 --> E1
  B2 --> E1
  B3 --> E1
  D_12 -.-> E1
  E1 --> E2
  B5 --> E2
  D_5 -.-> E2
  E1 --> E3
  G2 --> E4
  P1 --> E4
  B2 --> E4
  G6 --> E5
  B4 --> E5
  G6 --> E6
  G6 --> E7
  G6 --> E8
  B4 --> E8
  P0_3 --> V1
  C5 --> V2
  C6 --> V2
  D_4 -.-> V2
  C3 --> V3
  C7 --> V3
  E1 --> V3
  E1 --> V4
  D6 --> V4
  D_6 -.-> V4
  C6 --> V5
  D6 --> V5
  D6 --> V6
  D_7 -.-> V6
  P0_1 --> H1
  D_1 -.-> H1
  D_8 -.-> H1
  G3 --> H2
  D5 --> H2
  D_9 -.-> H2
  D4 --> H3
  D5 --> H3
  B1 --> H3
  E1 --> H3
  H2 --> H4
  P0_2 --> H4
  G5 --> H5
  D_10 -.-> H5
  G7 --> H6
  H4 --> H6
  P0_1 --> G0
  P0_2 --> G0
  P0_3 --> G0
  P0_4 --> G0
  A1 --> G1
  A2 --> G1
  A3 --> G1
  A4 --> G1
  A5 --> G1
  A6 --> G1
  A7 --> G1
  A8 --> G1
  A9 --> G1
  P0_5 --> G1
  C1 --> G2
  C2 --> G2
  C3 --> G2
  C4 --> G2
  C5 --> G2
  C6 --> G2
  C7 --> G2
  C8 --> G2
  B1 --> G2
  B2 --> G2
  B3 --> G2
  B6 --> G2
  A10 --> G2
  D1 --> G3
  D2 --> G3
  D3 --> G3
  D4 --> G3
  D5 --> G3
  D6 --> G3
  E1 --> G4
  E3 --> G4
  V3 --> G4
  G3 --> G5
  G4 --> G5
  V2 --> G5
  V5 --> G5
  V6 --> G5
  H1 --> G5
  H2 --> G5
  H3 --> G5
  H4 --> G5
  D_8 -.-> G5
  D_11 -.-> G5
  P1 --> G6
  P2 --> G6
  P3 --> G6
  P4 --> G6
  P5 --> G6
  P6 --> G6
  B4 --> G6
  E4 --> G7
  E5 --> G7
  E6 --> G7
  E7 --> G7
  E8 --> G7
  D_11 -.-> G7
  classDef gate fill:#dbeafe,stroke:#1d4ed8;
  classDef decision fill:#fef3c7,stroke:#b45309;
  class G0,G1,G2,G3,G4,G5,G6,G7 gate;
  class D_1,D_10,D_11,D_12,D_2,D_3,D_4,D_5,D_6,D_7,D_8,D_9 decision;
```
