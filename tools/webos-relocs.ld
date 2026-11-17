/* webOS 3's loader (glibc 2.19), binding everything at start (BIND_NOW), reads the two relocation tables as one: the
   section ld puts between them (.ARM.exidx) was taken for relocations and the app ended before main(). They are kept
   together, ahead of it; nothing else moves. Linked into the app, libsbengine.so and libsbdts.so (from PR #48 by alterbang) */
SECTIONS
{
  .rel.dyn : { *(.rel.dyn) }
  .rel.plt : { *(.rel.plt) }
}
INSERT BEFORE .ARM.exidx;
