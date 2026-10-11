# Literature and Software Survey — Piano and Plucked-String Physical Modelling

Author: Guilherme da Silveira · Dated 2026-10-11 · Status: background research. Cited by [implementation-plan.md](implementation-plan.md) and [engine-specification.md](engine-specification.md).

> Canonical copy of the survey answer that opened the planning conversation, migrated into this repository. Planning documents are written and maintained by Opus agents only (see [CLAUDE.md](../../CLAUDE.md)).

The richest seam is a French school (Chaigne, Joly, Chabassier, Boutillon) for full-piano and guitar simulation where material and geometry are explicit inputs. Belfast and Edinburgh (van Walstijn, Bilbao) lead on sitar/tanpura bridges and guitar frets. Findings are grouped by topic below.

## Piano: full models with material and geometry inputs

- **Chabassier, Chaigne & Joly, "Modeling and simulation of a grand piano"** (JASA 2013), plus "Time domain simulation of a piano," Parts 1–2 (ESAIM M2AN): nonlinear stiff strings, a dissipative orthotropic Reissner-Mindlin soundboard with ribs and bridges treated as local heterogeneities, coupled to the air, and validated against a Steinway D. This is the reference model.
- **Chabassier & Duruflé, Vienna Talk 2015**: uses that model to vary string parameters such as tension at rest and diameter, which changed significantly over the history of piano making. Their INRIA report "Physical parameters for piano modeling" is a useful data source.
- **MAESSTRO** (Elie, Cotté, Boutillon et al., Acta Acustica 2022): a "Sound Computer-Aided Design" program meant to predict sound changes from modifications to soundboard geometry or mechanical properties. It is software where you enter the virtual soundboard's geometry and materials in a GUI, feed it MIDI, and get synthesized tones. It is open source and the closest existing match to this project's goals.
- **Miranda Valiente, PhD thesis** (Southampton, 2025): a state-space modal hammer–string–soundboard model. Its parametric studies cover soundboard thickness, number of ribs and bridge height, and it also treats una corda.
- **Xie, "Physical Modeling of Piano Sound"** (arXiv 2024): builds on Chabassier, with hammer felt, shank, string, soundboard, air and room barriers each modelled in three dimensions.
- **Yamaha's in-house simulator** predicts how sound changes with design specs, modelling hammers, dampers, strings, soundboard, iron plate, rim, braces and air. It isn't public.

## Una corda specifically

- **Weinreich, "Coupled piano strings"** (JASA 1977) is the foundation. He first noted string coupling and also studied the una corda pedal's effects.
- **Miranda Valiente et al., "Modelling the una-corda effect in pianos"** (J. Phys. Conf. Ser. 2909, 2024): 3D stiff strings plus a reduced modal soundboard. It finds that with una corda the passive string's bridge force grows over time and contributes less to the total.

## Hammer and felt

- **Boutillon** (JASA 1988): a point mass plus a nonlinear hysteretic spring for the felt.
- **Stulov**: four- and three-parameter hysteretic felt models, plus a hammer testing device. His measured hammer elastic parameters depend strongly on string diameter.
- **Chaigne & Askenfelt**, "Numerical simulations of piano strings" I–II (JASA 1994). Part II systematically explores hammer–string parameters.
- **Chabassier & Duruflé** (JSV 2014): models the flexible hammer shank as a Timoshenko beam.

## Real-time and hybrid piano synthesis

- **Bank & Chabassier, "Model-Based Digital Pianos"** (IEEE SPM 2019) goes from physically accurate heavy models to optimized real-time ones. It's the best review to start with.
- **Bank, Zambon & Fontana** (IEEE TASLP 2010): a real-time modal synthesizer covering both transverse and longitudinal string motion.
- **Simionato, Fasciani & Holm** (Frontiers 2024) and **Simionato & Fasciani** (2025): physics-informed differentiable DSP whose parameters are estimated from recordings. The 2025 model adds phantom partials, beating and double decay.

## Software

- **Pianoteq (Modartt)**: Pro allows note-by-note editing of 22 physical parameters, including unison width, hammer hardness, strike point, string length, soundboard impedance and damper position. Soft-pedal level is among the voicing controls. String length can run from a 1 m mini grand to an imaginary 10 m one, and v8 added a classical guitar model. Chabassier consults on Pianoteq.
- **Csound `prepiano`** (Bilbao): a finite-difference model of 1–3 strings with detuning and hammer mass, with preparations modelled as lumped nonlinearities.
- **Modalys (IRCAM)**: modal synthesis that lets you take a string of any material and dimension. Its built-in piano soundboard comes from measurements, so its parameters can't be edited.
- **Faust physmodels.lib + mesh2faust**: mesh2faust turns a 3D mesh into a Faust modal model via FEM.
- **nlm** (Max externals, arXiv 2026): nonlinear modal string and plate that expose the physical parameters, with material/geometry presets.
- **Physical Audio**: Preparation 2 offers strings or bars colliding with a rattle and fret, with editable gap, mass and stiffness. There is also a guitar product (see Bilbao 2024 below).

## Guitar

- **Derveaux, Chaigne, Joly & Bécache** (JASA 2003): a 3D time-domain model with a heterogeneous orthotropic top. It reports the effect of soundboard thickness and cavity volume on the sound.
- **Bader, *Computational Mechanics of the Classical Guitar*** (Springer 2005): a finite-difference time-stepping model of the complete guitar geometry.
- **Shepherd et al.** (JASA 2014): an FE braced top plate with natural wood variability, including moisture dependence, and uncertainty bounds.
- **Viala et al.** (Spanish guitar FE): longitudinal and radial elastic parameters of the top dominate the dynamics, and humidity matters too.
- **"Towards Dynamic Copies of Guitars"** (2024): same-batch spruce soundboards still differed by about 5% in eigenfrequencies, which an FE model then compensates with geometry changes.
- **Bilbao & Torin** (DAFx 2014): tension modulation plus distributed collisions with the fretboard, frets and a stopping finger. **Bilbao et al.** (DAFx 2024) bring this to real time.

## Sitar, tanpura and other buzzing bridges

- **van Walstijn & Chatziioannou** (ISMA 2014): a tanpura model with string–bridge contact defined along the full bridge length.
- **Bridges & van Walstijn** (2015): a 2D version with coupling between polarisations and bridge friction.
- **van Walstijn, Bridges & Mehes** (DAFx 2016): a real-time tanpura model that includes the bridge and the cotton thread.
- **Issanchou et al.** (JASA 2018): nonsmooth contact dynamics, tested on a tanpura-like two-point bridge and an electric bass.
- **Kartofelev et al.** (2013): a waveguide string against curved bridges, covering sitar jawari, biwa and shamisen.
- **Ronan's sitar model**: there is a SuperCollider port.
- **Taguti** (2008): models the biwa/shamisen sawari as a unilateral constraint.
- **Shamisen real-time FDTD** (SMC 2021).

## Other plucked instruments

- **Harpsichord**: Välimäki et al. (EURASIP 2004) use waveguides plus a soundboard filter from measured bridge responses. Perng, Smith & Rossing (2010) model plectrum–string interaction with controllable physical parameters.
- **Kantele**: Erkut et al. (JASA 2002) trace its timbre to bridgeless and knotted terminations, which cause second-order nonlinearity and beating.
- **Tanbur**: a waveguide model with tension-modulation nonlinearity (ICASSP 2000).


## Sources

- [Modeling and simulation of a grand piano (HAL)](https://hal-ensta-paris.archives-ouvertes.fr/hal-00873089)
- [Chabassier & Duruflé, Vienna Talk 2015](https://viennatalk2015.mdw.ac.at/proceedings/ViennaTalk2015_submission_19.pdf)
- [MAESSTRO, Acta Acustica 2022](https://acta-acustica.edpsciences.org/articles/aacus/abs/2022/01/aacus210096/aacus210096.html)
- [MAESSTRO software (HAL)](https://hal-ensta-paris.archives-ouvertes.fr/hal-03409789)
- [Miranda Valiente PhD thesis](https://eprints.soton.ac.uk/504922/)
- [Modelling the una-corda effect](https://eprints.soton.ac.uk/499445)
- [Xie, Physical Modeling of Piano Sound](https://arxiv.org/html/2409.03481v1)
- [Yamaha piano simulation](https://www.yamaha.com/en/about/research/technologies/piano-simulation/)
- [Bank & Chabassier 2019](https://hal-univ-pau.archives-ouvertes.fr/hal-01894219)
- [Bank, Zambon & Fontana 2010](https://home.mit.bme.hu/~bank/publist/taslp-piano/index.html)
- [Simionato et al. 2024](https://doaj.org/article/bb49f130d4ed4ad3ab1b5ed3b73da097)
- [Pianoteq Pro parameters](https://rekkerd.org/?p=17368)
- [Pianoteq features](https://www.modartt.com/morphing)
- [Csound prepiano](https://csound.com/manual/opcodes/prepiano/)
- [Modalys introduction](https://support.ircam.fr/docs/Modalys/3.6/Introduction.html)
- [nlm, arXiv 2026](https://arxiv.org/pdf/2603.10240v2)
- [Derveaux et al., guitar](https://researchportal.ip-paris.fr/en/publications/time-domain-simulation-of-a-guitar-model-and-method/)
- [Bader, Springer](https://link.springer.com/book/10.1007/3-540-31103-3)
- [Shepherd et al. 2014](https://pure.psu.edu/en/publications/the-effects-of-wood-variability-on-the-free-vibration-of-an-acous/)
- [Viala et al.](https://biblioteca.iqs.edu/en/node/3767)
- [Bilbao & Torin, DAFx 2014](https://dafx.de/paper-archive/details/VW3ckMHQUdwpOen-KsadwA)
- [Real-time guitar synthesis, DAFx 2024](https://dafx.de/paper-archive/2024/papers/DAFx24_paper_50.pdf)
- [Real-time tanpura model, DAFx 2016](https://dafx.de/paper-archive/details/DYDzhePW7F9YEWfoZSF0dQ)
- [Issanchou et al. 2018](https://perso.ensta-paris.fr/~touze/PDF/2018_NonSmoothString_JASA.pdf)
- [Taguti, sawari](https://www.jstage.jst.go.jp/article/ast/29/3/29_3_203/_article)
- [Harpsichord synthesis, EURASIP 2004](https://asp-eurasipjournals.springeropen.com/articles/10.1155/S111086570440211X)
- [Kantele, JASA 2002](https://labs.sonicfield.org/library/acoustical-analysis-and-model-based-sound-synthesis-of-the-kantele)