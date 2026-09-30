#include "IdTable.h"

using namespace std;

IdTable::IdTable(int bucketCount) {
	buckets.assign(bucketCount, -1);
}

int IdTable::hash(const std::string& name) const {
	unsigned int h = 0;
	for (char c : name) {
		h = h * 31 + static_cast<unsigned char>(c);
	}
	return static_cast<int>(h % buckets.size());
}

int IdTable::addOrFind(const string& name, int line, int col) {
	int h = hash(name);
	int i = buckets[h];

	while (i != -1)
	{
		compareCount++;
		if (list[i].name == name) {
			list[i].count++;
			return i + 1;
		}
		i = list[i].next;
	}
	if (buckets[h] != -1) {
		collisionCount++;
	}

	list.push_back({ name, line,col, 1, buckets[h] });
	buckets[h] = list.size() - 1;
	return static_cast<int>(list.size());
}