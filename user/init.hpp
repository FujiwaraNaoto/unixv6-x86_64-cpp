#ifndef USER_INIT_HPP
#define USER_INIT_HPP

// リング3で動く init の本体。実体は init.cpp の .user セクションにある。
// カーネル側 (include/init.cpp) は usermode::enter() にこのアドレスを渡してリング3へ降りる。
void init_main();

#endif // USER_INIT_HPP
