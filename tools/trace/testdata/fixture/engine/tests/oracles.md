# Verification oracle registry (fixture)

| ID | Oracle | SPEC-ORC setup | Expected value | Tolerance | Owning phase | Gate | Status | Test location |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| ORC-01 | Oscillator accuracy | one mode | decay | 0.01 cent | A1 | G1 | passing | osc_tests.cpp |
| ORC-02 | Oscillator energy | lossless mode | constant | 1e-12 | A1 | G1 | not implemented | - |
