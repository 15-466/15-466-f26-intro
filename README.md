# GP26 Intro Mode

This is a splash screen for GP'26 games.

To use it, just construct one and set it as the initial mode. Pass a function that creates your *real* initial mode as a parameter:

```cpp
	//in main.cpp:
	Mode::set_current(
		std::make_shared< GP26IntroMode >( [](){
			Mode::set_current(std::make_shared< PlayMode >());
		} );
	);
```

This intro was built with [NEST](NEST.md).
