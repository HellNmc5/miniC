#pragma once
#include <string>
#include <vector>

struct IdEntry {
	std::string name;
	int line, col; // Где встретился впервый раз идентификатор
	int count;// Сколько раз встретился
	int next;// Индекс следующей записи в цепочке, !!!N-1!!! -1 - конец
};

class IdTable {
public:
	explicit IdTable(int bucketCount = 101);
	int addOrFind(const std::string& name, int line, int col);// возвращаем Номер
	const std::vector<IdEntry>& entries() const { return list; }
	
	//Получение статистики коллизий и сравнений
	int collisions() const { return collisionCount; }
	int comparisons() const { return compareCount; }
private:
	std::vector<int> buckets; //Голова цепочки корзины
	std::vector<IdEntry> list; //Все записи, которые нашли
	int collisionCount = 0;
	int compareCount = 0;

	int hash(const std::string& name) const;
};