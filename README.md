## 🛠️ Steps for compiling custom terminal and run the terminal.

```bash
    sudo apt install libxft-dev libfreetype6-dev
    sudo apt install fonts-noto fonts-dejavu

    export echo LANG=en_US.UTF-8

    g++ function.cpp -o function
    g++ terminal.cpp -o terminal -lX11 -lXft -I/usr/include/freetype2
    ./terminal
```
