## Steps for compiling custom terminal and run the terminal.

```bash
    sudo apt install libxft-dev libfreetype6-dev
    sudo apt install fonts-noto fonts-dejavu

    export LANG=en_US.UTF-8

	make icon
	make install-icon
    make file-icon
    make
  
    ./terminal
```

Or build and run in one step with `make run`. Use `make clean` to remove the built binaries.
