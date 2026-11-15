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

If you don't want to deal with splash screen during testing, you might be a bit fancier:
```cpp
//near the top of main():

bool do_skip_intro = false;

for (int argi = 1; argi < argc; ++argi) {
	std::string arg = argv[argi];
	if (arg == "--skip-intro") {
		do_skip_intro = true;
	} else {
		std::cerr << "WARNING: ignoring argument '" << arg << "'." << std::endl;
	}
}

//...

//and, for mode creation:

{ //create mode:
	auto intro = std::make_shared< GP26IntroMode >( [](){
		Mode::set_current(std::make_shared< PlayMode >());
	} );
	if (do_skip_intro) {
		intro->time = 10.0f; //force intro to end immediately
	}
	Mode::set_current( intro );
}
```

This intro was built with [NEST](NEST.md).
