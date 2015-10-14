# koji
A simple, small, fast scripting language.

This is still very much work in progress.

## Features

- Full implementation in single standard C source file.
- Heavily inspired by Lua and Javascript. Bytecode very similar to Lua's.
- Fast compilation and runtime (todo: verify it ;) )
- Prototype based, minimal language.
- C-like syntax! *Yay*!
- Short-circuit boolean and conditional expressions.
- No syntax-tree built for compilation: bytecode emitted as source is parsed.

## Example

	globals.point_metatype = {
		add: def (other)
		{
			this.x = this.x + other.x
			this.y = this.y + other.y
		}
	}

	var Point = def (x, y)
	{
		var p = { x: x, y: y }
		set_metatype(p, globals.point_metatype)
		return p
	}

	var p1 = Point(10, 10)
	var p2 = Point(12, 12)

	p1.add(p2)
