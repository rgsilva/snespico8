// Calypsi 5.17 cc65816 -O2 (large models): in the comma expression
//   (save(ls, ls->current), next(ls))
// where next() modifies ls->current, the argument load of ls->current
// is hoisted past the next() call: save() receives the ALREADY-ADVANCED
// character. Lua's lexer saved "ocal " when scanning "local ". C11
// guarantees left-to-right sequencing across the comma operator.
// Workaround: do { save(ls, ls->current); next(ls); } while (0).
// (Minimal standalone repro not yet reduced; observed in lua-5.2.4
// llex.c save_and_next with sequenced-store save() variant.)
