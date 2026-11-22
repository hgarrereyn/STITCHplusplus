import stitch

Stmt = stitch.object("Stmt")      # sqlparse.sql.Statement
Tok = stitch.object("Tok")        # a token or token list from a tree
SqlText = stitch.datatype("SqlText")


@stitch.decoder(SqlText)
def sql_text(s):
    parts = []
    while s.more():
        k = s.choice(6)
        if k == 0:
            parts.append(s.token())
        elif k == 1:
            parts.append(s.str(10))
        elif k == 2:
            parts.append(" ")
        elif k == 3:
            parts.append(f"{s.token()} {s.token()}")
        elif k == 4:
            parts.append(str(s.range(0, 999)))
        else:
            parts.append(s.pick(["select", "from", "t", "a.b", "x", "(", ")", ",", ";", "'s'"]))
    return " ".join(parts)
