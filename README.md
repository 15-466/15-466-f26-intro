# GP26 Intro Mode

This is a splash screen for GP'26 games.

Mostly self-contained (and, otherwise, written against our standard base code).

To use, copy `GP26IntroMode.*pp` into your project, add to your `Maekfile.js`, and set as the initial mode in `main.cpp`. Pass a function that creates your *real* initial mode as a parameter:

```cpp
	//in main.cpp:
	Mode::set_current(
		std::make_shared< GP26IntroMode >( [](){
			Mode::set_current(std::make_shared< PlayMode >());
		} );
	);
```

This intro was built with [NEST](NEST.md).
