# ChrisOS GLSL subset

`#version 330` is accepted as a syntax tag for this subset. That line does not mean the compiler is GLSL 3.30 compliant. There is no conformance suite.

| Feature | Status |
| --- | --- |
| Vertex shader | supported, host-tested, used by the VirGL proof scene |
| Fragment shader | supported, host-tested, used by the VirGL proof scene |
| Geometry, tessellation, compute | unsupported, rejected |
| `#version` 110–330 | supported, profile text such as `core` is ignored |
| Other `#` directives | unsupported, diagnosed |
| `//` and `/* */` comments | supported |
| `int`, `float`, `bool` | supported |
| `vec2`, `vec3`, `vec4` | supported |
| `ivec2`, `ivec3`, `ivec4` | parsed as types; most arithmetic is scalar or float |
| `mat3`, `mat4` | supported, column-major |
| `sampler2D` and `texture()` | supported in the fragment shader |
| `double`, `uint`, `dvec`, `uvec`, `mat2`, `samplerCube`, `sampler3D` | unsupported |
| `in`, `out`, `uniform`, `const` | supported |
| `layout(location = N)` | supported |
| `smooth` | accepted, same as the default perspective varying |
| `flat`, `noperspective`, `attribute`, `varying`, precision qualifiers | unsupported, diagnosed |
| Constructors `vec2/3/4`, `mat3/4`, `float()`, `int()` | supported, including `vec4(position, 1.0)` and `mat4(1.0)` |
| Swizzle `.xyzw` and `.rgba` | supported, out-of-range components are errors |
| `+ - * /` on scalars, vectors, and the matrix products below | supported |
| `mat4 * vec4`, `mat4 * mat4`, `mat3 * vec3` | supported |
| `%` | only when both sides are constant integers |
| `&&` `||` | both sides are evaluated |
| `if` / `else` | supported |
| `for` | supported only for a constant trip count of at most 8, otherwise an error |
| `discard` | supported in fragment shaders, host-tested |
| `return` | must be the last statement of the function |
| User functions | inlined, recursion rejected |
| `gl_Position` | required write in a vertex shader |
| `gl_FragCoord` | available in a fragment shader |
| `texture`, `dot`, `cross`, `length`, `normalize`, `abs`, `sin`, `cos`, `min`, `max`, `clamp`, `mix`, `pow`, `reflect` | supported |
| Multiple render targets | unsupported, one fragment `out vec4` |
| `gl_FragColor`, `gl_FragData` | unsupported; declare `out vec4` |

Host evidence is `make host-shader-test`. VirGL evidence is the serial log from `make test-qemu-virgl` for the shaders that demo actually draws. `sin` and `discard` are host-tested only.
