#pragma once
template<typename T>
class Singleton
{
protected:
	Singleton() = default;
	virtual ~Singleton() = default;
public:
	static T* Instance()
	{
		static T instance;
		return &instance;
	}
	Singleton(const Singleton&) = delete;
	Singleton& operator=(const Singleton&) = delete;

	virtual void Init() {}
	virtual void Release() {}
};