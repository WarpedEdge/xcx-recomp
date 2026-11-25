"""Verified facial-hair experiment hooks, XCX DE 2.0.0 only."""
import avatar_hair

WORDS = {0x962e24: 0xf9400808}

def verified(mod):
    if mod.name != 'main' or mod.nso.build_id != avatar_hair.BUILD:
        return False
    if mod.nso.source_sha256 != avatar_hair.SHA256 or mod.base != 0x7000000000:
        raise ValueError('Avatar beard: build identity mismatch')
    for offset, word in WORDS.items():
        if mod.word(mod.base + offset) != word:
            raise ValueError(f'Avatar beard: instruction mismatch at {offset:#x}')
    return True

def entry(mod, address):
    if address - mod.base == 0x962e24:
        return 'avatar_beard_component(c);'
    return ''
