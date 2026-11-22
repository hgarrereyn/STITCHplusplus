import Ink

_ = MarkdownParser().parse("---\n\n")   // metadata block never closed: reader.currentCharacter at end -> trap
