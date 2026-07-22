#include <vector>
#include <cstddef>
#include <algorithm>

class MedianFilterInt {
public:
    MedianFilterInt(size_t window_size) 
        : size(window_size), head(0) 
    {
        if (size % 2 == 0) size++;
        history.resize(size, 0);
        sorted.resize(size, 0);
    }

    int process(int current_fill)
    {
        int oldest = history[head];
        history[head] = current_fill;
        head = (head + 1) % size;
	size_t i = 0;
        while (i < size && sorted[i] != oldest) i++;
        if (i == size) i = 0;
	sorted[i] = current_fill;
	while (i > 0 && sorted[i] < sorted[i - 1])
	{
            std::swap(sorted[i], sorted[i - 1]);
            i--;
        }
        while (i < size - 1 && sorted[i] > sorted[i + 1])
	{
            std::swap(sorted[i], sorted[i + 1]);
            i++;
        }
	return sorted[size / 2];
    }

    void reset(int initial_value = 0)
    {
        std::fill(history.begin(), history.end(), initial_value);
        std::fill(sorted.begin(), sorted.end(), initial_value);
        head = 0;
    }

private:
    size_t size;
    size_t head;
    std::vector<int> history;
    std::vector<int> sorted;
};